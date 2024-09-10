"use strict";

function all() {
    const PRESETS = 6;
    let debug = document.getElementById("debug");
    let slider = document.getElementById("levelslider");
    let levelind = document.getElementById('ajaxlevel');
    let sppowerind = document.getElementById('sppower');
    let currentpowerind = document.getElementById('currentpower');
    let spbyteind = document.getElementById('spbyte');
    let sppercentind = document.getElementById('sppercent');
    let currentbyteind = document.getElementById('currentbyte');
    let currentpercentind = document.getElementById('currentpercent');
    let poweronind = document.getElementById("poweron-level");
    let offmessage_el = document.getElementById("offmessage");
    let presetbuttons = Array.from(document.getElementsByClassName("levelbutton"));
    let fadebuttons = Array.from(document.getElementsByClassName("fadebutton"));

    let currentfade_el = document.getElementById("currentfadeinner");
    let levelbox = document.getElementById("levelbutton-box");
    let offbox = document.getElementById("offbutton-box");
    let onbox = document.getElementById("onbutton-box");
    let override_warning_el = document.getElementById("override-warning");
    let log_el = document.getElementById("log");
    let log_header_el = document.getElementById("loghead");
    let timermessage = "Turning Power Off in 2 minutes... Alarms will NOT wake.";
    const offmessage = "Alarms will NOT wake";

    let powerStatus = true;
    function levelbyte_to_linear(byte) {
        return Math.floor(Math.pow(10, (byte - 1) * 3.0 / 253.0) * 10 + 0.5) / 100;
    }

    function levelToPercent(level) {
        return Math.floor(level / 2.54 + 0.5) + "%";

    }
    function stylePresets(status_ob, fade) {
        let level;
        if (status_ob) {
            level = status_ob.setpoint;
            currentbyteind.innerHTML = status_ob.actual_level;
            currentpercentind.innerHTML = levelToPercent(status_ob.actual_level);
            sppercentind.innerHTML = levelToPercent(status_ob.setpoint);
            spbyteind.innerHTML = status_ob.setpoint;
            sppowerind.innerHTML = `${status_ob.setpoint_power.toLocaleString(undefined,
                { minimumFractionDigits: 3 })}` + "W";
            currentpowerind.innerHTML = `${status_ob.actual_power.toLocaleString(undefined,
                { minimumFractionDigits: 3 })}` + "W";
        
            let percentstr = levelToPercent(status_ob.setpoint);
            levelind.innerHTML = percentstr;
            poweronind.innerHTML = percentstr;
            log_el.innerHTML = status_ob.logstring;
            log_header_el.innerHTML = status_ob.logheader;
            override_warning_el.style.display = status_ob.overrides ? "block" : "none";
        } else {
            level = undefined;
        }

        if (level !== undefined) {
            presetbuttons.forEach(el => {
                let preset_level = parseInt(el.getAttribute("level"));
                let setpoint_level_int = parseInt(level);
                let tolerance = preset_level > 2 ? 2 : 0;
                let isinrange = (preset_level >= (setpoint_level_int - tolerance)) && (preset_level <= (setpoint_level_int + tolerance));
                if (el.classList.contains("slow") && el.id != "sbuttonoff") {
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
    for (let i = 1; i <= PRESETS; i++) {
        let uri = `/nvs/preset/${i}/`;
        preset_promises.push(get(uri).then((st) => {
            let button_el = document.getElementById("preset" + i);
            let slutton_el = button_el.nextElementSibling;
            if (st >= 0) {
                button_el.setAttribute("level", st);
                slutton_el.setAttribute("level", st);
                button_el.childNodes[1].innerHTML = levelToPercent(st);
            }
        }));
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
        if (uri == undefined) {
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
                return response.json();
            })
            .then(json_ob => {
                stylePresets(json_ob);
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    };

    function sliderChangeFn(event) {
        let sliderval = slider.value;
        let percentstr = levelToPercent(sliderval);
        levelind.innerHTML = percentstr;
        spbyteind.innerHTML = sliderval;
        // currentind.innerHTML = levelbyte_to_linear(sliderval) + "%";
        poweronind.innerHTML = percentstr;
        levelind.classList.add("updating");
        // debug.innerHTML = "slider.e " + sliderval + " Cancellable: " + event.cancelable;

        if (event.cancelable == true) {
            return;
        }
        send(sliderval, undefined, undefined, "slider");
        if (!ontimeout) {
            debug.innerHTML = "slider.e " + sliderval + " " + event.cancelable;
            ontimeout = 1;
            setTimeout(function () {
                ontimeout = 0;
                if (waiting) {
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
        if (level != undefined) {
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
        if (event.srcElement.type == "submit") {
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
                stylePresets({"setpoint":spbyteind.innerHTML});
            });
            return;
        }

        console.log(element);
        let level = element.getAttribute("level");
        slider.value = level;
        let percentstr = levelToPercent(level);
        levelind.innerHTML = percentstr;
        // currentind.innerHTML = levelbyte_to_linear(level) + "%";
        poweronind.innerHTML = percentstr;
        // levelbyteind.innerHTML = level;
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

    function get_state(skip_styling) {
        let setpoint_promise = get("/setpoint/all-json").then(text => {
            let status_ob = JSON.parse(text);
            let sp = status_ob.setpoint;
            let return_value = true;
            if (!(sp >= 0)) {
                sp = "0"
                return_value = false;
            }
            if (!skip_styling) stylePresets(status_ob);
            slider.value = sp;
            return status_ob;
        });
        get("/api/power").then((sp) => {
            powerStatus = sp == "1";
            if (!powerStatus && (sp != "0")) return;
            if (powerStatus) {
                setUIPowerOn();
            } else {
                setUIPowerOff();
                if (sp == "Timer on") {

                    offmessage_el.innerHTML = timermessage;
                    window.setTimeout(() => {
                        offmessage_el.innerHTML = offmessage;
                    }, 60 * 1000 * 2);
                }
            };
            return true;
        });
        let fadetime_promise = get("/nvs/slow_fade").then((st) => {
            if (st > 0){
                stylePresets(undefined, st / 1000);
                return true;
            }
        });
        let all = Promise.all([fadetime_promise, setpoint_promise]);
        all.catch(() => {
            console.log("catch");
            document.getElementsByTagName("main")[0].classList.add("loading");
        });
        all.then((r) => {
            console.log("then", r);
            if (r[0] && r[1]){
                document.getElementsByTagName("main")[0].classList.remove("loading");
                return true;
            }
            document.getElementsByTagName("main")[0].classList.add("loading");
            return false;
        });

        window.setTimeout(get_state, 3000);
        return all;
    }
    let state_promise = get_state(true);
    preset_promises.push(state_promise);

    Promise.all(preset_promises).then((p) => {
        stylePresets(p[PRESETS][1]);
        document.getElementsByTagName("main")[0].classList.remove("loading");
    });
}

window.onload = all;