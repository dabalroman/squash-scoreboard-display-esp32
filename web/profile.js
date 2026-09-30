// Filled from GET /api/roster. R[i] = [name, colour, uid]; PAL[k] = [name, '#RRGGBB'].
var MAX = 32,
    PAL = [],
    R = [];

function esc (s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// R[i][1] is a palette index (number) for a preset colour, or a
// '#RRGGBB' string for a custom one - see /api/roster and isPreset().
function hx (c) {return typeof c == 'number' ? PAL[c][1] : c;}

function render () {
    var h = '';
    for (var i = 0; i < R.length; i++) {
        h += '<div class="row">'
            + '<button type="button" class="sw" id="sw'
            + i
            + '" style="background:'
            + hx(R[i][1])
            + '" onclick="pick('
            + i
            + ')"></button>'
            + '<input maxlength="9" value="'
            + esc(R[i][0])
            + '" oninput="setName('
            + i
            + ',this.value)">'
            + '<button type="button" class="ic" onclick="mv('
            + i
            + ',-1)">&#9650;</button>'
            + '<button type="button" class="ic" onclick="mv('
            + i
            + ',1)">&#9660;</button>'
            + '<button type="button" class="ic del" onclick="del('
            + i
            + ')">&times;</button></div>';
    }
    document.getElementById('rows').innerHTML = h;
    document.getElementById('cnt').textContent = R.length + ' / ' + MAX;
}

// No re-render on keystroke: that would drop focus mid-word.
function setName (i, v) {R[i][0] = v;}

// cur = row being edited; orig = its colour when the dialog opened
// (Anuluj target); sel = the pending selection (Wybierz target).
var cur = -1,
    orig = 0,
    sel = 0;
// Preview on open: tapping a row's colour shows that player on the LEDs at once.
// Opens on the tab that matches the stored colour: custom hex -> sliders.
function pick (i) {
    cur = i;
    orig = sel = R[i][1];
    renderPal();
    tab(typeof sel == 'string');
    document.getElementById('pal').style.display = 'flex';
    preview(i, hx(sel));
}

function renderPal () {
    var h = '';
    for (var k = 0; k < PAL.length; k++) {
        h +=
            '<button type="button" class="pc'
            + (typeof sel == 'number' && sel == k ? ' on' : '')
            + '" onclick="swatchClick('
            + k
            + ')">'
            + '<i style="background:'
            + PAL[k][1]
            + '"></i><span>'
            + esc(PAL[k][0])
            + '</span></button>';
    }
    document.getElementById('palgrid').innerHTML = h;
// 75 % of full white (765): near-white draws the most current per LED;
// saturated two-channel colours like #00FFFF (510) stay below it.
    var x = hx(sel),
        t = parseInt(x.substr(1, 2), 16) + parseInt(x.substr(3, 2), 16) + parseInt(x.substr(5, 2), 16);
    document.getElementById('palwarn').style.display = t >= 574 ? 'block' : 'none';
// Under 10 % of full white a WS2812 is barely lit at all.
    document.getElementById('paldark').style.display = t < 77 ? 'block' : 'none';
}

function swatchClick (k) {
    sel = k;
    renderPal();
    preview(cur, PAL[k][1]);
}

// Exact-inverse pair, no rounding of h/s/v before the reverse math - only the
// final byte gets Math.round - or hex2hsv(hsv2hex(x)) drifts off x by 1 LSB.
function hsv2hex (h, s, v) {
    s = s / 100;
    v = v / 100;
    var c = v * s,
        x = c * (1 - Math.abs((h / 60) % 2 - 1)),
        m = v - c,
        r,
        g,
        b;
    if (h < 60) {
        r = c;
        g = x;
        b = 0;
    } else if (h < 120) {
        r = x;
        g = c;
        b = 0;
    } else if (h < 180) {
        r = 0;
        g = c;
        b = x;
    } else if (h < 240) {
        r = 0;
        g = x;
        b = c;
    } else if (h < 300) {
        r = x;
        g = 0;
        b = c;
    } else {
        r = c;
        g = 0;
        b = x;
    }

    function h2 (n) {
        var b2 = Math.round((n + m) * 255);
        if (b2 < 0) {
            b2 = 0;
        }
        if (b2 > 255) {
            b2 = 255;
        }
        var s2 = b2.toString(16);
        return b2 < 16 ? '0' + s2 : s2;
    }

    return ('#' + h2(r) + h2(g) + h2(b)).toUpperCase();
}

function hex2hsv (hex) {
    var r = parseInt(hex.substr(1, 2), 16) / 255,
        g = parseInt(hex.substr(3, 2), 16) / 255,
        b = parseInt(hex.substr(5, 2), 16) / 255;
    var mx = Math.max(r, g, b),
        mn = Math.min(r, g, b),
        d = mx - mn,
        h = 0;
    if (d != 0) {
        if (mx == r) {
            h = 60 * (((g - b) / d) % 6);
        } else if (mx == g) {
            h = 60 * ((b - r) / d + 2);
        } else {
            h = 60 * ((r - g) / d + 4);
        }
    }
    if (h < 0) {
        h += 360;
    }
    return [h, mx == 0 ? 0 : d / mx * 100, mx * 100];
}

// Track gradients show the effect of each slider at the other two's current
// values; the swatch mirrors what Wybierz would commit.
var HV = {
        hh: 0,
        hs: 100,
        hv: 100
    },
    HM = {
        hh: 359,
        hs: 100,
        hv: 100
    };

function syncHsv () {
    var h = HV.hh,
        s = HV.hs,
        v = HV.hv;
    for (var id in HV) {
        document.getElementById(id).parentNode.querySelector('b').style.left =
            (HV[id] / HM[id] * 100) + '%';
    }
    document.getElementById('hs').style.background =
        'linear-gradient(to right,' + hsv2hex(h, 0, v) + ',' + hsv2hex(h, 100, v) + ')';
    document.getElementById('hv').style.background =
        'linear-gradient(to right,#000,' + hsv2hex(h, s, 100) + ')';
    var hex = hsv2hex(h, s, v);
    document.getElementById('hswi').style.background = hex;
    document.getElementById('hswt').textContent = hex;
}

function hsvInput () {
    sel = hsv2hex(HV.hh, HV.hs, HV.hv);
    syncHsv();
    renderPal();
    preview(cur, sel);
}

function slide (id) {
    var el = document.getElementById(id).parentNode;

    function at (e) {
        var r = el.getBoundingClientRect(),
            f = (e.clientX - r.left) / r.width;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        var n = Math.round(f * HM[id]);
        if (n != HV[id]) {
            HV[id] = n;
            hsvInput();
        }
    }

    el.addEventListener('pointerdown', function (e) {
        el.setPointerCapture(e.pointerId);
        e.preventDefault();
        at(e);
    });
    el.addEventListener('pointermove', function (e) {
        if (el.hasPointerCapture(e.pointerId)) {
            at(e);
        }
    });
}

slide('hh');
slide('hs');
slide('hv');

// Switching to the sliders seeds them from the current selection.
function tab (c) {
    if (c) {
        var v = hex2hsv(hx(sel));
        HV.hh = v[0];
        HV.hs = v[1];
        HV.hv = v[2];
        syncHsv();
    }
    document.getElementById('palgrid').style.display = c ? 'none' : 'grid';
    document.getElementById('hsvbox').style.display = c ? 'flex' : 'none';
    document.getElementById('tabpal').classList.toggle('on', !c);
    document.getElementById('tabcus').classList.toggle('on', !!c);
}

document.getElementById('tabpal').onclick = function () {tab(false);};
document.getElementById('tabcus').onclick = function () {tab(true);};

function closePal () {
    document.getElementById('pal').style.display = 'none';
    cur = -1;
}

document.getElementById('palok').onclick = function () {
    R[cur][1] = sel;
    document.getElementById('sw' + cur).style.background = hx(sel);
    closePal();
};
document.getElementById('palclose').onclick = function () {
    preview(cur, hx(orig));
    closePal();
};
document.getElementById('pal').onclick = function (e) {
    if (e.target.id == 'pal') {
        preview(cur, hx(orig));
        closePal();
    }
};
// One /preview in flight; a colour arriving mid-request replaces
// whatever was pending rather than queuing, so drag events never pile up.
var pvBusy = false,
    pvNext = null;

function preview (i, h) {
    pvNext = [i, h];
    if (!pvBusy) {
        pvSend();
    }
}

function pvSend () {
    if (!pvNext) {
        return;
    }
    var n = pvNext;
    pvNext = null;
    pvBusy = true;
    var x = new XMLHttpRequest();
    x.open('POST', '/preview', true);
    x.timeout = 3000;
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onloadend = function () {
        pvBusy = false;
        pvSend();
    };
// Best effort: a dropped preview leaves the LEDs one frame behind,
// never the page - so failures are silent by design.
    x.send('playerId=' + n[0] + '&color=' + encodeURIComponent(n[1]));
}

function mv (i, d) {
    var j = i + d;
    if (j < 0 || j >= R.length) {
        return;
    }
    var t = R[i];
    R[i] = R[j];
    R[j] = t;
    render();
}

function del (i) {
    if (R.length < 2) {
        msg('Musi zostać co najmniej jeden profil.', 'err');
        return;
    }
    R.splice(i, 1);
    render();
}

// k: '' neutral, 'ok' or 'err' - the shared .msg line.
function msg (t, k) {
    var m = document.getElementById('msg');
    m.textContent = t;
    m.className = 'msg' + (k ? ' ' + k : '');
}

function post (b) {
    var x = new XMLHttpRequest();
    x.open('POST', '/save', true);
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onload = function () {
        if (x.status == 200) {
            msg(x.responseText, 'ok');
        } else {
            msg('Błąd ' + x.status + ': ' + x.responseText, 'err');
        }
    };
    x.onerror = function () {msg('Połączenie przerwane - tablica prawdopodobnie się restartuje.', 'err');};
    x.send(b);
}

document.getElementById('add').onclick = function () {
    if (R.length >= MAX) {
        msg('Osiągnięto limit 32.', 'err');
        return;
    }
    R.push(['Gracz' + (R.length + 1), R.length % PAL.length, 0]);
    render();
};
document.getElementById('save').onclick = function () {
    for (var i = 0; i < R.length; i++) {
        if (!R[i][0].trim()) {
            msg('Wiersz ' + (i + 1) + ': brak imienia.', 'err');
            return;
        }
    }
// Label-colon form: Polish numerals decline, so "N profili" is wrong for some N.
    if (!confirm('Zapisać listę. Liczba pozycji: ' + R.length
        + '. Tablica zostanie zrestartowana.')) {
        return;
    }
    var b = 'count=' + R.length;
    for (var i = 0; i < R.length; i++) {
        b += '&n' + i + '=' + encodeURIComponent(R[i][0].trim())
            + '&c' + i + '=' + encodeURIComponent(R[i][1]) + '&u' + i + '=' + R[i][2];
    }
    msg('Zapisywanie...');
    post(b);
};
document.getElementById('reset').onclick = function () {
    if (!confirm('Przywrócić profile fabryczne? Tablica zostanie zrestartowana.')) {
        return;
    }
    msg('Przywracanie...');
    post('reset=1');
};
// The page is static; the data comes from the gated API. A 503 means the editor
// is shut on the board, which gets the note instead - the nav stays usable.
// `t` overrides the note's text for a malformed-response failure (parse or
// render threw on a 200) - a gate 503 keeps its normal PROFILE/Dev Mode wording.
function closed (t) {
    var c = document.getElementById('closed');
    if (t) {
        c.textContent = t;
    }
    c.style.display = 'block';
}

function load () {
    var x = new XMLHttpRequest();
    x.open('GET', '/api/roster', true);
    x.onload = function () {
        if (x.status == 200) {
            try {
                var d = JSON.parse(x.responseText);
                MAX = d.max;
                PAL = d.pal;
                R = d.rows;
                render();
                document.getElementById('editor').style.display = '';
            } catch (e) {closed('Nie udało się wczytać profili. Odśwież stronę.');}
            return;
        }
        closed();
    };
    x.onerror = function () {closed();};
    x.send();
}

load();
