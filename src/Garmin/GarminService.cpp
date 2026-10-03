#include "GarminService.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <version.h>

#include "GarminLink.h"
#include "GarminStore.h"
#include "RemoteDevelopmentService/LoggerHelper.h"

namespace {
    portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

    // Held only for byte copies inside GarminLink: it masks interrupts on the calling core,
    // which on core 1 includes the RMT refill ISR.
    struct MuxGuard {
        MuxGuard() { portENTER_CRITICAL(&gMux); }
        ~MuxGuard() { portEXIT_CRITICAL(&gMux); }
        MuxGuard(const MuxGuard &) = delete;
        MuxGuard &operator=(const MuxGuard &) = delete;
    };

    typedef GarminLinkT<MuxGuard> GarminLink;

    void fillRandom(uint8_t *out, const size_t len) {
        esp_fill_random(out, len);
    }

    uint32_t randomWord() {
        return esp_random();
    }

    GarminLink gLink(fillRandom, randomWord);

    enum class BleState : uint8_t { Off, Starting, Running, Stopping, Failed };

    // Written by the core-0 tasks, read by loop().
    volatile BleState gState = BleState::Off;
    volatile bool gStopRequested = false;
    volatile uint8_t gInitError = 0;
    volatile uint32_t gHeapBefore = 0;
    volatile uint32_t gHeapAfter = 0;
    volatile uint16_t gStateAttr = 0;
    volatile uint16_t gPairingAttr = 0;
    uint32_t gStartedAt = 0;
    bool gBlockedThisBoot = false;

    // Set across NimBLE init. Survives a panic, watchdog or software reset (not a power-on), so
    // a reset inside init - its ESP_ERROR_CHECKs abort - keeps BLE off on every boot until a
    // power cycle, instead of crashing a board that is only reachable over OTA every other boot.
    RTC_NOINIT_ATTR uint32_t gInitMarker;
    constexpr uint32_t INIT_MARKER = 0x47524D4Eu;

    // Built in begin() (loop), read by the init task it then creates.
    Garmin::Frame gVersion;
    Garmin::AdvData gAdv;
    Garmin::AdvData gScanResponse;
    char gName[12];

    constexpr uint32_t TASK_STACK = 8192;
    // NimBLE init took 51.7 KB on V1 (#80 gate); below this it is not attempted.
    constexpr uint32_t MIN_INTERNAL_HEAP = 80 * 1024;
    constexpr uint32_t INIT_STALL_MS = 5000;
    constexpr uint32_t PERSIST_RETRY_MS = 5000;
    constexpr uint32_t FAILURE_LOG_MS = 10000;

    enum InitError : uint8_t { InitOk = 0, InitAdvertising = 1 };

    uint32_t internalHeap() {
        return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    // Per connection, from loop(): NimBLECharacteristic::notify() walks a subscriber vector
    // the host task mutates, so it is never used on core 1. A NULL mbuf (pool exhausted) is
    // a failure like any other.
    bool sendFrame(const uint16_t connHandle, const uint16_t attrHandle, const Garmin::Frame &frame) {
        os_mbuf *om = ble_hs_mbuf_from_flat(frame.data, frame.len);
        if (om == nullptr) return false;
        return ble_gattc_notify_custom(connHandle, attrHandle, om) == 0;
    }

    class ServerCallbacks : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
            if (!gLink.connect(desc->conn_handle, millis())) {
                ble_gap_terminate(desc->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            // A connection stops advertising; keep it up below the limit. Disconnects
            // re-advertise through NimBLEServer itself.
            if (server->getConnectedCount() < Garmin::MAX_CONNECTIONS) {
                NimBLEDevice::startAdvertising();
            }
        }

        void onDisconnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
            gLink.disconnect(desc->conn_handle);
        }
    };

    enum class Chr : uint8_t { Command, State, Roster, Auth, Pairing };

    // Host task. Every write is length-checked by the GarminLink parsers before anything
    // is touched; what an unauthenticated connection may do is decided there.
    class CharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        explicit CharCallbacks(const Chr kind) : kind(kind) {}

        void onRead(NimBLECharacteristic *chr, ble_gap_conn_desc *desc) override {
            const uint16_t handle = desc->conn_handle;
            Garmin::Frame frame;
            switch (kind) {
                case Chr::State:
                    frame = gLink.readState(handle);
                    break;
                case Chr::Roster:
                    frame = gLink.readRoster(handle);
                    break;
                case Chr::Auth:
                    frame = gLink.readAuth(handle);
                    break;
                case Chr::Pairing:
                    frame = gLink.readPairing(handle);
                    break;
                default:
                    return;
            }
            // The value is shared by all connections; the host task serves reads one at a
            // time, so setting it right before NimBLE copies it out is per connection.
            chr->setValue(frame.data, frame.len);
        }

        void onWrite(NimBLECharacteristic *chr, ble_gap_conn_desc *desc) override {
            const NimBLEAttValue value = chr->getValue();
            const uint16_t handle = desc->conn_handle;
            switch (kind) {
                case Chr::Command:
                    gLink.writeCommand(handle, value.data(), value.size());
                    break;
                case Chr::Roster:
                    gLink.writeRoster(handle, value.data(), value.size());
                    break;
                case Chr::Auth:
                    gLink.writeAuth(handle, value.data(), value.size(), millis());
                    break;
                case Chr::Pairing:
                    gLink.writePairing(handle, value.data(), value.size(), millis());
                    break;
                default:
                    break;
            }
        }

        void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc, const uint16_t subValue) override {
            const bool notify = (subValue & 0x0001) != 0;
            if (kind == Chr::State) {
                gLink.subscribe(desc->conn_handle, GarminLink::Sub::State, notify);
            } else if (kind == Chr::Pairing) {
                gLink.subscribe(desc->conn_handle, GarminLink::Sub::Pairing, notify);
            }
        }

    private:
        Chr kind;
    };

    ServerCallbacks serverCallbacks;
    CharCallbacks commandCallbacks(Chr::Command);
    CharCallbacks stateCallbacks(Chr::State);
    CharCallbacks rosterCallbacks(Chr::Roster);
    CharCallbacks authCallbacks(Chr::Auth);
    CharCallbacks pairingCallbacks(Chr::Pairing);

    NimBLECharacteristic *addCharacteristic(NimBLEService *service, const char *uuid, const uint32_t properties,
                                            NimBLECharacteristicCallbacks *callbacks) {
        // max_len bounds the write buffer NimBLE puts on the host task's stack.
        NimBLECharacteristic *chr = service->createCharacteristic(uuid, properties, Garmin::MAX_FRAME);
        if (callbacks != nullptr) chr->setCallbacks(callbacks);
        return chr;
    }

    // Core 0 task context only.
    void teardown() {
        ble_gap_adv_stop();
        uint16_t handles[Garmin::MAX_CONNECTIONS];
        const uint8_t n = gLink.connectedHandles(handles);
        for (uint8_t i = 0; i < n; i++) ble_gap_terminate(handles[i], BLE_ERR_REM_USER_CONN_TERM);
        // Lets the watches see a clean disconnect rather than a supervision timeout.
        if (n > 0) vTaskDelay(pdMS_TO_TICKS(300));
        NimBLEDevice::deinit(true);
        gLink.resetConnections();
        gStateAttr = 0;
        gPairingAttr = 0;
        gHeapAfter = internalHeap();
    }

    // NimBLEDevice::init() spins until the host syncs, so it never runs on loop()'s core.
    void initTask(void *) {
        gInitMarker = INIT_MARKER;
        gHeapBefore = internalHeap();

        NimBLEDevice::init(gName);
        NimBLEServer *server = NimBLEDevice::createServer();
        server->setCallbacks(&serverCallbacks, false);
        server->advertiseOnDisconnect(true);

        NimBLEService *service = server->createService(Garmin::SERVICE_UUID);
        NimBLECharacteristic *version = addCharacteristic(service, Garmin::VERSION_UUID, NIMBLE_PROPERTY::READ, nullptr);
        version->setValue(gVersion.data, gVersion.len);
        addCharacteristic(service, Garmin::COMMAND_UUID, NIMBLE_PROPERTY::WRITE, &commandCallbacks);
        NimBLECharacteristic *state = addCharacteristic(service, Garmin::STATE_UUID,
                                                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
                                                        &stateCallbacks);
        addCharacteristic(service, Garmin::ROSTER_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE,
                          &rosterCallbacks);
        addCharacteristic(service, Garmin::AUTH_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE, &authCallbacks);
        NimBLECharacteristic *pairing = addCharacteristic(
            service, Garmin::PAIRING_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY,
            &pairingCallbacks);
        service->start();
        // Attribute handles exist once the GATT server has started.
        server->start();
        gStateAttr = state->getHandle();
        gPairingAttr = pairing->getHandle();

        // Spec 4.3: both payloads explicit, so NimBLE adds neither the UUID nor the name.
        NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
        NimBLEAdvertisementData advData;
        advData.addData(reinterpret_cast<char *>(gAdv.data), gAdv.len);
        NimBLEAdvertisementData scanData;
        scanData.addData(reinterpret_cast<char *>(gScanResponse.data), gScanResponse.len);
        advertising->setAdvertisementData(advData);
        advertising->setScanResponseData(scanData);
        const bool started = advertising->start();

        gInitMarker = 0;
        gHeapAfter = internalHeap();

        if (!started) {
            gInitError = InitAdvertising;
            teardown();
            gState = BleState::Failed;
        } else if (gStopRequested) {
            // Stopping, not Starting: a begin() from here on sets restartPending.
            gState = BleState::Stopping;
            teardown();
            gState = BleState::Off;
        } else {
            gState = BleState::Running;
        }
        vTaskDelete(nullptr);
    }

    void stopTask(void *) {
        teardown();
        gState = BleState::Off;
        vTaskDelete(nullptr);
    }
}

void GarminService::load() {
    uint8_t blob[sizeof(GarminData)];
    size_t len = 0;
    const bool found = GarminStore::read(blob, len);
    const bool valid = gLink.loadData(found ? blob : nullptr, len);
    printLn("Garmin: %s, %u watches paired%s", gLink.enabled() ? "enabled" : "disabled",
            static_cast<unsigned>(gLink.pairedCount()), found && !valid ? " (stored blob rejected)" : "");
}

void GarminService::setRoster(const std::vector<UserProfile *> &profiles) {
    uint8_t entries[32 * Garmin::ROSTER_ENTRY_SIZE];
    uint8_t count = 0;
    for (const UserProfile *profile : profiles) {
        if (count >= 32) break;
        const Color color = profile->getColor();
        Garmin::buildRosterEntry(profile->getUid(), color.r, color.g, color.b, profile->getName(),
                                 entries + static_cast<size_t>(count) * Garmin::ROSTER_ENTRY_SIZE);
        count++;
    }
    gLink.setRoster(count, entries);
}

bool GarminService::isEnabled() const {
    return gLink.enabled();
}

void GarminService::setEnabled(const bool on) {
    gLink.setEnabled(on);
    if (on) {
        begin();
    } else {
        end();
    }
}

void GarminService::begin() {
    if (gState == BleState::Starting) {
        // Cancels an end() that came while init was still running.
        gStopRequested = false;
        return;
    }
    if (gState == BleState::Stopping) {
        // loop() starts it again once the stop task has finished.
        restartPending = true;
        return;
    }
    if (gBlockedThisBoot || gState == BleState::Running) {
        return;
    }

    if (gInitMarker == INIT_MARKER) {
        if (esp_reset_reason() == ESP_RST_POWERON) {
            gInitMarker = 0; // RTC memory holds garbage after a power-on
        } else {
            // Kept set: blocked on every boot until a power cycle clears it.
            gBlockedThisBoot = true;
            gState = BleState::Failed;
            printLn("Garmin: BLE off - a start was cut by a reset; power-cycle the board to retry");
            return;
        }
    }

    const uint32_t heap = internalHeap();
    if (heap < MIN_INTERNAL_HEAP) {
        gState = BleState::Failed;
        printLn("Garmin: BLE not started, internal heap %u B", static_cast<unsigned>(heap));
        return;
    }

    gLink.ensureBoardId();
    const uint32_t boardId = gLink.boardId();
    bool open;
    uint16_t code;
    // The payload below already carries the current window.
    gLink.takeAdvChanged(open, code);
    gAdv = Garmin::buildAdvertising(boardId, gLink.pairingOpen(), gLink.pairingCode());
    gScanResponse = Garmin::buildScanResponse(boardId);
    gVersion = Garmin::buildVersion(FW_VERSION);
    snprintf(gName, sizeof(gName), "Score-%04X", static_cast<unsigned>(boardId & 0xFFFF));

    gStopRequested = false;
    gInitError = InitOk;
    stallLogged = false;
    gStartedAt = millis();
    gState = BleState::Starting;

    if (xTaskCreatePinnedToCore(initTask, "garminInit", TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
        gState = BleState::Off;
        printLn("Garmin: BLE init task not created");
    }
}

void GarminService::end() {
    restartPending = false;
    gLink.closePairing();

    if (gState == BleState::Starting) {
        // The init task tears down on its own once init returns.
        gStopRequested = true;
        return;
    }
    if (gState == BleState::Failed && !gBlockedThisBoot) {
        gState = BleState::Off;
        return;
    }
    if (gState != BleState::Running) {
        return;
    }

    gState = BleState::Stopping;
    if (xTaskCreatePinnedToCore(stopTask, "garminStop", TASK_STACK, nullptr, 1, nullptr, 0) != pdPASS) {
        gState = BleState::Running;
        printLn("Garmin: BLE stop task not created");
    }
}

bool GarminService::running() const {
    return gState == BleState::Running;
}

void GarminService::loop(const uint32_t now) {
    // printLn belongs to loop() (telnet is not core-0 safe), so the tasks only leave state.
    const uint8_t state = static_cast<uint8_t>(gState);
    if (state != lastLoggedState) {
        lastLoggedState = state;
        switch (static_cast<BleState>(state)) {
            case BleState::Running:
                printLn("Garmin: BLE on, advertising as %s, internal heap %u -> %u B", gName,
                        static_cast<unsigned>(gHeapBefore), static_cast<unsigned>(gHeapAfter));
                break;
            case BleState::Failed:
                if (gInitError == InitAdvertising) {
                    printLn("Garmin: BLE start failed (advertising), feature off");
                }
                break;
            case BleState::Off:
                printLn("Garmin: BLE off, internal heap %u B", static_cast<unsigned>(gHeapAfter));
                break;
            default:
                break;
        }
    }
    if (restartPending && gState == BleState::Off) {
        restartPending = false;
        begin();
    }
    // Reconcile with the stored flag: a begin()/end() racing the init task's last check can
    // otherwise leave BLE off while enabled, or running while disabled.
    if (gState == BleState::Off && gLink.enabled()) {
        begin();
    } else if (gState == BleState::Running && (!gLink.enabled() || gStopRequested)) {
        gStopRequested = false;
        end();
    }
    if (gState == BleState::Starting && !stallLogged && now - gStartedAt > INIT_STALL_MS) {
        stallLogged = true;
        printLn("Garmin: BLE init has not synced after %u ms", static_cast<unsigned>(now - gStartedAt));
    }

    if (!persistBackoff || now - persistFailedAt >= PERSIST_RETRY_MS) {
        GarminData data;
        if (gLink.takeDirtyData(data)) {
            persistBackoff = !GarminStore::write(data);
            if (persistBackoff) {
                persistFailedAt = now;
                gLink.markDataDirty();
                printLn("Garmin: NVS write failed, retrying");
            }
        }
    }

    if (gState != BleState::Running) {
        return;
    }

    gLink.tick(now);

    bool open;
    uint16_t code;
    if (gLink.takeAdvChanged(open, code)) {
        const Garmin::AdvData adv = Garmin::buildAdvertising(gLink.boardId(), open, code);
        const int rc = ble_gap_adv_set_data(adv.data, adv.len);
        if (rc != 0) {
            printLn("Garmin: advertising update failed (%d)", rc);
        }
    }

    uint16_t handles[Garmin::MAX_CONNECTIONS];
    const uint8_t due = gLink.dueDisconnects(now, handles);
    for (uint8_t i = 0; i < due; i++) {
        ble_gap_terminate(handles[i], BLE_ERR_REM_USER_CONN_TERM);
    }

    GarminLink::Notify notify;
    while (gLink.takePairingNotify(notify)) {
        sendFrame(notify.handle, gPairingAttr, notify.frame);
    }

    const uint32_t failures = gLink.failedPushes();
    if (failures != loggedFailures && now - failureLoggedAt >= FAILURE_LOG_MS) {
        printLn("Garmin: %u state notifications failed (retried)", static_cast<unsigned>(failures - loggedFailures));
        loggedFailures = failures;
        failureLoggedAt = now;
    }
}

bool GarminService::takeCommand(uint16_t &handle, WatchCommand &command) {
    if (gState != BleState::Running) {
        return false;
    }
    GarminLink::Queued queued;
    if (!gLink.takeCommand(queued)) {
        return false;
    }
    handle = queued.handle;
    command = queued.cmd;
    return true;
}

void GarminService::acknowledge(const uint16_t handle, const uint8_t seq, const Garmin::AckStatus status) {
    gLink.acknowledge(handle, seq, status);
}

void GarminService::rejectQueued() {
    if (gState == BleState::Running) {
        gLink.rejectQueued(Garmin::AckStatus::Busy);
    }
}

void GarminService::publish(const WatchState &state, const uint32_t now) {
    if (gState == BleState::Running) {
        sendPushes(&state, now);
    }
}

void GarminService::publishAcks(const uint32_t now) {
    if (gState == BleState::Running) {
        sendPushes(nullptr, now);
    }
}

void GarminService::sendPushes(const WatchState *state, const uint32_t now) {
    GarminLink::Push pushes[Garmin::MAX_CONNECTIONS];
    const uint8_t n = gLink.preparePushes(state, now, pushes);
    for (uint8_t p = 0; p < n; p++) {
        const GarminLink::Push &push = pushes[p];
        const uint8_t chunks = Garmin::chunkCount(push.len);
        for (uint8_t i = 0; i < chunks; i++) {
            const Garmin::Frame frame = Garmin::buildStateChunk(push.stateSeq, push.body, push.len, i);
            if (!sendFrame(push.handle, gStateAttr, frame)) {
                // The rest of this set is not sent; the retry is a whole new set.
                gLink.pushFailed(push.handle, now);
                break;
            }
        }
    }
}

uint16_t GarminService::openPairing() {
    return gLink.openPairing(millis());
}

void GarminService::closePairing() {
    gLink.closePairing();
}

bool GarminService::pairingOpen() const {
    return gLink.pairingOpen();
}

uint16_t GarminService::pairingCode() const {
    return gLink.pairingCode();
}

uint32_t GarminService::pairingRemainingMs() const {
    return gLink.pairingRemainingMs(millis());
}

uint8_t GarminService::authedCount() const {
    return gLink.authedCount();
}

uint32_t GarminService::completedPairings() const {
    return gLink.completedPairings();
}

uint8_t GarminService::pairedCount() const {
    return gLink.pairedCount();
}

void GarminService::forgetAll() {
    gLink.forgetAll(millis());
}
