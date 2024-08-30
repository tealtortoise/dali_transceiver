"use strict";

function all(){
    
    let debug = document.getElementById("debug");
    let slider = document.getElementById("levelslider");
    let levelind = document.getElementById('ajaxlevel');
    let currentind = document.getElementById('ledcurrent');
    let levelbyteind = document.getElementById('levelbyte');
    let poweronind = document.getElementById("poweron-level");
    let offmessage_el = document.getElementById("offmessage");
    let presetbuttons = Array.from(document.getElementsByClassName("levelbutton"));
    let fadebuttons = Array.from(document.getElementsByClassName("fadebutton"));

    let currentfade_el = document.getElementById("currentfadeinner");
    let levelbox = document.getElementById("levelbutton-box");
    let offbox = document.getElementById("offbutton-box");
    let onbox = document.getElementById("onbutton-box");
    let timermessage = "Turning Power Off in 2 minutes... Alarms will NOT wake.";
    const offmessage = "Alarms will NOT wake";

    let powerStatus = true;
    function levelbyte_to_linear(byte){
        return Math.floor(Math.pow(10, (byte-1) * 3.0 / 253.0) * 10 + 0.5) / 100;
    }

    function stylePresets(level, fade) {
        if (level !== undefined){
            presetbuttons.forEach(el => {
                let bl = parseInt(el.getAttribute("level"));
                level = parseInt(level);
                let isinrange = (bl >= (level - 2)) && (bl <= (level + 2));
                if (el.classList.contains("slow")) {
                    if (isinrange) {
                        el.innerHTML = "";
                        el.style.opacity = "0.4";
                    } else {
                        el.innerHTML = "Slow";
                        el.style.opacity = "1.0";
                    }
                } else {
                    if (isinrange) {
                        el.classList.add("selected");
                    } else {
                        el.classList.remove("selected");
                    }
                }
            });
        }
        if (fade === undefined) return;
        fadebuttons.forEach(el => {
            if (el.getAttribute("speed") == fade) {
                el.classList.add("selected");
            } else {
                el.classList.remove("selected");
            }
        });
        currentfade_el.innerHTML = `${fade}`;
    }

    function get(uri, el) {
        const options = {
        method: 'GET',
        };
        return fetch(uri, options)
            .then(response => {
                if (!response.ok) {
                    return "";
                }
                let text = response.text()
                return text;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    }
    var ontimeout = 0;
    var waiting = 0;
    let preset_promises = [];
    for (let i = 1; i <= 5; i++) {
        let uri = `/nvs/preset/${i}/`;
        preset_promises.push(get(uri).then((st) => {
            let button_el = document.getElementById("preset" + i);
            let slutton_el = button_el.nextElementSibling;
            if (st >= 0){
                button_el.setAttribute("level", st);
                slutton_el.setAttribute("level", st);
                button_el.childNodes[1].innerHTML = levelToPercent(st);
            }
        }));
    }

    function levelToPercent(level) {
        return Math.floor(level / 2.54 + 0.5) + "%";

    }

    function send(data, slow, uri, source) {
        // debug.innerHTML = source + (new Date(Date.now()));
        const options = {
            method: 'PUT',
            headers: {
                'Content-Type': 'application/plain'
            },
            body: `${data}`
        };
        let use_uri;
        // Make the PUT request using the fetch API
        if (uri == undefined){
            if (slow) {
                use_uri = "/setpoint/slow";
            } else {
                use_uri = "/setpoint"
            }
        } else {
            use_uri = uri;
        }
        return fetch(use_uri, options)
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                levelind.classList.remove("updating");
                if (!uri) {
                    stylePresets(data);
                }
                return response;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    };

    function sliderChangeFn(event) {
        let sliderval = slider.value;
        let percentstr = levelToPercent(sliderval);
        levelind.innerHTML = percentstr;
        levelbyteind.innerHTML = sliderval;
        currentind.innerHTML = levelbyte_to_linear(sliderval) + "%";
        poweronind.innerHTML = percentstr;
        levelind.classList.add("updating");
        debug.innerHTML = "slider.e " + sliderval + " Cancellable: " + event.cancelable;
        
        if (event.cancelable == true){
            return;
        }
        send(sliderval, undefined, undefined, "slider");
        if (!ontimeout){
            debug.innerHTML = "slider.e " + sliderval + " " + event.cancelable;
            ontimeout = 1;
            setTimeout(function() {
                ontimeout = 0;
                if (waiting){
                    sliderval = slider.value;
                    // debug.innerHTML = "slider.e " + sliderval;
                    send(sliderval, undefined, undefined, "slidertimeout");
                    waiting = 0;
                }
            }, 80);
        } else {
            waiting = 1;
        }
    };

    function sliderSendFn(event) {
        let sliderval = slider.value;
        send(sliderval, undefined, undefined, "slidergo");
    }

    function setUIPowerOff(timeout) {
        powerStatus = false;
        if (!timeout) offmessage_el.innerHTML = offmessage;
        let box = document.getElementById("levelbutton-box");
        box.style.opacity = "0.6";
        let offbox = document.getElementById("offbutton-box");
        let onbox = document.getElementById("onbutton-box");
        offbox.style.display = "none";
        onbox.style.display = "grid";
    }
    function setUIPowerOn() {
        powerStatus = true;
        let box = document.getElementById("levelbutton-box");
        box.style.opacity = "1.0";
        let offbox = document.getElementById("offbutton-box");
        let onbox = document.getElementById("onbutton-box");
        onbox.style.display = "none";
        offbox.style.display = "grid";
    }
    function sendPowerOn(level) {
        let uri;
        if (level != undefined){
            uri = "api/power/setlevel";
        } else {
            uri = "api/power";
            level = 1;
        }
        return send(level, false, uri, "sendpoweron");
    }

    function fadeButtonFn(event) {
        let element = event.srcElement;
        let speed = element.getAttribute("speed");
        send(speed * 1000, undefined, "/nvs/slow_fade");
        stylePresets(undefined, speed);
    }

    function buttonPressFn(event, v2) {
        console.log(event);
        let element;
        let slow;
        if (event.srcElement.type == "submit")
        {
            element = event.srcElement;
            slow = event.srcElement.classList.contains("slow");
        } else {
            element = event.srcElement.parentNode;
            slow = event.srcElement.parentNode.classList.contains("slow");
        }
        if (element.id == "buttonoff") {
            send("0", undefined, "api/power", "buttonoff ev").then(() => {
                setUIPowerOff();
            });
            return;
        }
        if (element.id == "sbuttonoff") {
            send(60 * 2 * 1000, undefined, "api/power/delay", "sbuttonoff").then(() => {
                setUIPowerOff(true);
                offmessage_el.innerHTML = timermessage;
                window.setTimeout(() => {
                    offmessage_el.innerHTML = "Alarms are OFF";
                }, 60 * 1000 * 2);
            });
            return;
        }
        if (element.id == "buttonon") {
            sendPowerOn().then(() => {
                setUIPowerOn();
                stylePresets(levelbyteind.innerHTML);
            });
            return;
        }
        
        console.log(element);
        let level = element.getAttribute("level");
        slider.value = level;
        let percentstr = levelToPercent(level);
        levelind.innerHTML = percentstr;
        currentind.innerHTML = levelbyte_to_linear(level) + "%";
        poweronind.innerHTML = percentstr;
        levelbyteind.innerHTML = level;
        levelind.classList.add("updating");
        if (powerStatus) {
            send(level, slow, undefined, `main buttonev ${slow}`);
        } else {
            sendPowerOn(level);
            setUIPowerOn();
        }
        console.log(level)
    }

    Array.from(document.getElementsByClassName("levelbutton")).forEach(element => {
        element.onclick = buttonPressFn;
    });

    Array.from(document.getElementsByClassName("fadebutton")).forEach(element => {
        element.onclick = fadeButtonFn;
    });

    document.getElementById("go").onclick = sliderSendFn;
    slider.addEventListener("input", sliderChangeFn);

    function get_state(skip_styling){
        let setpoint_promise = get("/setpoint").then((sp) => {
            if (!(sp >= 0)) {
                sp = "0"
            }
            if (!skip_styling) stylePresets(sp);
            let percentstr = levelToPercent(sp);
            levelind.innerHTML = percentstr;
            currentind.innerHTML = levelbyte_to_linear(sp) + "%";
            poweronind.innerHTML = percentstr;
            levelbyteind.innerHTML = sp;
            slider.value = sp;
        });
        get("/api/power").then((sp) => {
            powerStatus = sp == "1";
            if (!powerStatus && (sp != "0")) return;
            if (powerStatus) {
                setUIPowerOn();
            } else {
                setUIPowerOff();
                if (sp == "Timer on"){

                    offmessage_el.innerHTML = timermessage;
                    window.setTimeout(() => {
                        offmessage_el.innerHTML = offmessage;
                    }, 60 * 1000 * 2);
                }
            }
        });
        let fadetime_promise = get("/nvs/slow_fade").then((st) => 
        {
            stylePresets(undefined, st / 1000);
        });
        window.setTimeout(get_state, 20000);
        return Promise.all([fadetime_promise, setpoint_promise]);
    }
    preset_promises.push(get_state(true));
    
    Promise.all(preset_promises).then(() => {
        console.log("All promises resolved!");
        stylePresets(levelbyteind.innerHTML);
        document.getElementsByTagName("main")[0].classList.remove("loading");
    });
}

window.onload = all;