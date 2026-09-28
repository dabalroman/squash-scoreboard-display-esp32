var busy=false,bad=null,t0=0;
function el(i){return document.getElementById(i);}
// k: '' neutral, 'ok' or 'err' - the shared .msg line.
function om(t,k){var m=el('otamsg');m.textContent=t;m.className='msg'+(k?' '+k:'');}
function bar(p){var b=el('otabar');b.style.display=p<0?'none':'block';if(p>=0)b.value=p;}
function hideForce(){bad=null;el('otaforce').style.display='none';}
el('fw').onchange=function(){hideForce();om('');bar(-1);};
function send(f,force){
busy=true;hideForce();el('otasend').disabled=true;bar(0);
om('Wysyłanie pliku...');
var d=new FormData();d.append('update',f,f.name);
var x=new XMLHttpRequest();
x.open('POST','/update'+(force?'?force=1':''),true);
// Progress only. 100 % here means the browser has finished writing to its
// own socket, not that the board has finished reading it, so nothing is
// ever decided on this event - the verdict arrives in onload.
x.upload.onprogress=function(e){if(e.lengthComputable)bar(Math.round(e.loaded*100/e.total));};
x.onload=function(){busy=false;el('otasend').disabled=false;
if(x.status==200){bar(100);
om('Wgrano. Tablica restartuje się, poczekaj...','ok');t0=Date.now();wait();return;}
bar(-1);om(x.responseText||('Błąd '+x.status),'err');
// Only a refused image can be forced. A connection that broke tells us
// nothing about the file, so it gets no bypass.
if(x.status==400){bad=f;el('otaforce').style.display='block';}};
x.onerror=x.onabort=function(){busy=false;el('otasend').disabled=false;bar(-1);hideForce();
om('Połączenie przerwane. Sprawdź, czy telefon jest nadal połączony z siecią tablicy, i spróbuj ponownie.','err');};
x.send(d);}
// Any answer at all proves the board is back.
function wait(){var x=new XMLHttpRequest();
x.open('GET','/?ping='+Date.now(),true);x.timeout=4000;
x.onload=function(){location.reload();};
x.onerror=x.ontimeout=function(){
if(Date.now()-t0>120000){om('Tablica nie odpowiada. Połącz telefon ponownie z siecią tablicy i odśwież stronę.','err');return;}
setTimeout(wait,2000);};x.send();}
el('otasend').onclick=function(){if(busy)return;
var f=el('fw').files[0];
if(!f){om('Najpierw wybierz plik .bin.','err');return;}
if(!/\.bin$/i.test(f.name)){om('To nie jest plik .bin.','err');return;}
send(f,false);};
el('otaforce').onclick=function(){if(busy||!bad)return;
if(!confirm('Ten plik nie przeszedł sprawdzenia. Wgranie go może sprawić, że tablica przestanie działać. Wgrać mimo to?'))return;
send(bad,true);};
// Version from the ungated /api/device.
(function(){var x=new XMLHttpRequest();x.open('GET','/api/device',true);
x.onload=function(){if(x.status!=200)return;var d=JSON.parse(x.responseText);
el('fwver').textContent=d.fw;};
x.send();})();
