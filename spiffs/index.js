"use strict";

function all(){
    let powerStatus = true;
    function levelbyte_to_linear(byte){
        return Math.floor(Math.pow(10, (byte-1) * 3.0 / 253.0) * 10 + 0.5) / 100;
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
                // let text = response.text()
                return response.text();
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    }
    var ontimeout = 0;
    var waiting = 0;
    let debug = document.getElementById("debug");
    let slider = document.getElementById("levelslider");
    let levelind = document.getElementById('ajaxlevel');
    let currentind = document.getElementById('ledcurrent');
    let levelbyteind = document.getElementById('levelbyte');
    let poweronind = document.getElementById("poweron-level");
    let offmessage_el = document.getElementById("offmessage");

    let levelbox = document.getElementById("levelbutton-box");
    let offbox = document.getElementById("offbutton-box");
    let onbox = document.getElementById("onbutton-box");
    let timermessage = "Turning Power Off in 2 minutes... Alarms will be OFF.";

    for (let i = 1; i <= 5; i++) {
        let uri = `/nvs/preset/${i}/`;
        get(uri).then((st) => {
            let button_el = document.getElementById("preset" + i);
            let slutton_el = button_el.nextElementSibling;
            if (st >= 0){
                button_el.setAttribute("level", st);
                slutton_el.setAttribute("level", st);
                button_el.childNodes[1].innerHTML = levelToPercent(st);
            }
        });
    }

    function levelToPercent(level) {
        return Math.floor(level / 2.54 + 0.5) + "%";

    }

    function send(data, slow, uri, source) {
        debug.innerHTML = source + (new Date(Date.now()));
        const options = {
            method: 'PUT',
            headers: {
                'Content-Type': 'application/plain'
            },
            body: `${data}`
        };

        // Make the PUT request using the fetch API
        if (uri == undefined){
            if (slow) {
                uri = "/setpoint/slow";
            } else {
                uri = "/setpoint"
            }
        }
        return fetch(uri, options)
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                levelind.classList.remove("updating");
                return response;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    };

    function sliderChangeFn(event) {
        // if(event.cancelable == true){
            // return;
        // }
        let sliderval = slider.value;
        let percentstr = levelToPercent(sliderval);
        levelind.innerHTML = percentstr;
        levelbyteind.innerHTML = sliderval;
        currentind.innerHTML = levelbyte_to_linear(sliderval) + "%";
        poweronind.innerHTML = percentstr;
        levelind.classList.add("updating");
        if (!ontimeout){
            send(sliderval, undefined, undefined, "slider");
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

    function setUIPowerOff(timeout) {
        powerStatus = false;
        if (!timeout) offmessage_el.innerHTML = "Alarms are OFF";
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

    document.getElementById("go").onclick = sliderChangeFn;
    slider.onclick = sliderChangeFn;

    function get_state(){
        get("/setpoint").then((sp) => {
            if (!(sp >= 0)) {
                sp = "0"
            }
            let percentstr = levelToPercent(sp);
            levelind.innerHTML = percentstr;
            currentind.innerHTML = levelbyte_to_linear(sp) + "%";
            poweronind.innerHTML = percentstr;
            levelbyteind.innerHTML = sp;
            slider.value = sp;
        });
        get("/api/power").then((sp) => {
            powerStatus = sp == "1";
            if (powerStatus) {
                setUIPowerOn();
            } else {
                setUIPowerOff();
                if (sp == "Timer on"){

                    offmessage_el.innerHTML = timermessage;
                    window.setTimeout(() => {
                        offmessage_el.innerHTML = "Alarms are OFF";
                    }, 60 * 1000 * 2);
                }
            }
        });
        window.setTimeout(get_state, 20000);
    }
    get_state(); 
}

window.onload = all;