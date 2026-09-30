// S = stored values, V = what the page shows. Both from /api/settings; null while
// the gate is shut (the Wi-Fi form still works then).
var S = null,
    V = null;
var LV_MAX = 8;

function el (i) {return document.getElementById(i);}

// k: '' neutral, 'ok' or 'err' - the shared .msg line.
function say (id, t, k) {
    var m = el(id);
    m.textContent = t;
    m.className = 'msg' + (k ? ' ' + k : '');
}

// Handle position and its own digit - not <input type=range>, same .sl
// pointer-capture pattern as profile.js's HSV sliders (a finger drifting off a
// native range drops the drag).
function renderLevel () {
    var b = el('lvsl').querySelector('b');
    b.style.left = ((V.level - 1) / (LV_MAX - 1) * 100) + '%';
    b.textContent = V.level;
// Levels 7-8 are the highest draw, like the colour dialog's bright warning.
    el('lvwarn').style.display = V.level >= 7 ? 'block' : 'none';
}

function render () {
    renderLevel();
    el('bz1').classList.toggle('on', V.buzzer == 1);
    el('bz2').classList.toggle('on', V.buzzer == 2);
    el('bz0').classList.toggle('on', V.buzzer == 0);
    el('dm1').classList.toggle('on', V.devMode == 1);
    el('dm0').classList.toggle('on', V.devMode == 0);
}

function copy (o) {
    return {
        level: o.level,
        buzzer: o.buzzer,
        devMode: o.devMode
    };
}

// One request in flight; a newer preview replaces the pending one rather than
// queuing. The save goes through the same queue, so a late level can never
// land on the LEDs after it.
var busy = false,
    next = null,
    after = null,
    live = false;

function pv (b) {
    next = b;
    if (!busy) {
        send();
    }
}

function send () {
    if (!next) {
        if (after) {
            var f = after;
            after = null;
            f();
        }
        return;
    }
    var b = next;
    next = null;
    busy = true;
    var x = new XMLHttpRequest();
    x.open('POST', '/settings/preview', true);
    x.timeout = 3000;
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
// Best effort: a dropped preview leaves the LEDs one step behind, never the page.
    x.onloadend = function () {
        busy = false;
        send();
    };
    x.send(b);
}

function level (n) {
    if (n == V.level) {
        return;
    }
    V.level = n;
    render();
    live = true;
    say('msg', '');
    pv('level=' + n);
}

// Discrete snap from a horizontal fraction of the track, 1..LV_MAX.
function levelAt (e) {
    var r = el('lvsl').getBoundingClientRect();
    var f = (e.clientX - r.left) / r.width;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return Math.round(f * (LV_MAX - 1)) + 1;
}

(function () {
    var s = el('lvsl');

    function at (e) {level(levelAt(e));}

    s.addEventListener('pointerdown', function (e) {
        s.setPointerCapture(e.pointerId);
        e.preventDefault();
        at(e);
    });
    s.addEventListener('pointermove', function (e) {
        if (s.hasPointerCapture(e.pointerId)) {
            at(e);
        }
    });
})();
el('bz1').onclick = function () {
    V.buzzer = 1;
    render();
};
el('bz2').onclick = function () {
    V.buzzer = 2;
    render();
};
el('bz0').onclick = function () {
    V.buzzer = 0;
    render();
};
el('dm1').onclick = function () {
    V.devMode = 1;
    render();
};
el('dm0').onclick = function () {
    V.devMode = 0;
    render();
};

// Panel 1 sends the STORED devMode: only the developer panel may change it.
function save () {
    var b = 'level=' + V.level + '&buzzer=' + V.buzzer + '&devMode=' + S.devMode;
    var x = new XMLHttpRequest();
    x.open('POST', '/settings', true);
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onload = function () {
        if (x.status == 200) {
            S.level = V.level;
            S.buzzer = V.buzzer;
            live = false;
            say('msg', x.responseText, 'ok');
        } else {
            say('msg', 'Błąd ' + x.status + ': ' + x.responseText, 'err');
        }
    };
    x.onerror = function () {say('msg', 'Połączenie przerwane. Sprawdź połączenie z siecią tablicy.', 'err');};
    x.send(b);
}

el('save').onclick = function () {
    say('msg', 'Zapisywanie...');
    next = null;
    if (busy) {
        after = save;
    } else {
        save();
    }
};
// Leaving without saving puts the stored brightness back; the board also does it
// when PROFILE closes, and after 60 s idle outside PROFILE.
window.addEventListener('pagehide', function () {
    if (!live) {
        return;
    }
    live = false;
    if (navigator.sendBeacon) {
        navigator.sendBeacon('/settings/preview', new URLSearchParams('cancel=1'));
    }
});

// Developer panel: POST /settings/dev saves and restarts. devMode is sent only
// when the gate was open at load (V set) - the board refuses it otherwise.
var devBusy = false,
    t0 = 0;
el('devform').onsubmit = function (e) {
    e.preventDefault();
    if (devBusy) {
        return;
    }
    if (!el('ssid').value) {
        say('devmsg', 'Podaj nazwę sieci Wi-Fi.', 'err');
        return;
    }
    if (!confirm('Zapisać ustawienia? Tablica zostanie zrestartowana.')) {
        return;
    }
    var b = 'ssid=' + encodeURIComponent(el('ssid').value) + '&password=' + encodeURIComponent(el('pass').value);
    if (V) {
        b += '&devMode=' + V.devMode;
    }
    devBusy = true;
    el('devsave').disabled = true;
    say('devmsg', 'Zapisywanie...');
    var x = new XMLHttpRequest();
    x.open('POST', '/settings/dev', true);
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onload = function () {
        if (x.status == 200) {
            say('devmsg', 'Zapisano. Tablica restartuje się...', 'ok');
// The restart is deferred on the board; the first ping waits past it.
            t0 = Date.now();
            setTimeout(wait, 2000);
            return;
        }
        devBusy = false;
        el('devsave').disabled = false;
        say('devmsg', 'Błąd ' + x.status + ': ' + x.responseText, 'err');
    };
    x.onerror = function () {
        devBusy = false;
        el('devsave').disabled = false;
        say('devmsg', 'Połączenie przerwane. Sprawdź połączenie z siecią tablicy.', 'err');
    };
    x.send(b);
};

// As update.js: any answer at all proves the board is back.
function wait () {
    var x = new XMLHttpRequest();
    x.open('GET', '/?ping=' + Date.now(), true);
    x.timeout = 4000;
    x.onload = function () {location.reload();};
    x.onerror = x.ontimeout = function () {
        if (Date.now() - t0 > 120000) {
            say(
                'devmsg',
                'Tablica nie odpowiada. Jeśli zmieniono sieć Wi-Fi lub tryb deweloperski, połącz telefon z właściwą siecią i odśwież stronę.',
                'err'
            );
            return;
        }
        setTimeout(wait, 2000);
    };
    x.send();
}

function closed () {el('closed').style.display = 'block';}

(function () {
    var x = new XMLHttpRequest();
    x.open('GET', '/api/settings', true);
    x.onload = function () {
        if (x.status != 200) {
            closed();
            return;
        }
        S = JSON.parse(x.responseText);
        V = copy(S);
// '' falls back to the stylesheet's flex layout.
        el('editor').style.display = '';
        el('devset').style.display = '';
        render();
    };
    x.onerror = closed;
    x.send();
})();
// The Wi-Fi form is ungated; only its SSID prefill needs the (ungated) device info.
(function () {
    var x = new XMLHttpRequest();
    x.open('GET', '/api/device', true);
    x.onload = function () {
        if (x.status != 200) {
            return;
        }
        el('ssid').value = JSON.parse(x.responseText).ssid;
    };
    x.send();
})();
