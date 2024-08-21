
"use strict";
const NUM_ALARMS = 6;
function all() {
    let getEl = (st) => document.getElementById(st);
    let modified = false;

    let inputs = Array.from(document.getElementsByTagName("input"));
    function pad(num) {
        num = num.toString();
        while (num.length < 2) num = "0" + num;
        return num;
    }
    function get(uri, el, process) {
        
        if (!process) process = (_) => { return _; };
        const options = {
            method: 'GET',
        };
        return fetch(uri, options)
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                // let text = response.text()
                return response.text();
            })
            .then((st) => {
                if (el != null) {
                    el.value = process(st);
                    el.setAttribute("serverval", process(st));
                    console.log("Got ", st, uri, el, el.value);
                    return;
                }
                return st;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    }

    function send(uri, value, el, process) {
        if (!process) process = (_) => { return _; };
        if (el) {
            let ui_value = el.type == "checkbox" ? el.checked : el.value;
            let serverval = el.getAttribute("serverval");
            if (ui_value == serverval) return Promise.resolve();
        }
        if (value == undefined && el != undefined) value = el.value;
        const options = {
            method: 'PUT',
            headers: {
                'Content-Type': 'application/plain'
            },
            body: `${process(value)}`
        };

        // Make the PUT request using the fetch API
        return fetch(uri, options)
            .then(response => {
                if (!response.ok) {
                    throw new Error('Network response was not ok');
                }
                if (el != null) {
                    if (el.type == "checkbox") {
                        console.log(el);
                        let label = el.nextElementSibling;
                        label.classList.remove("modified");
                        label.classList.add("saved");
                    } else {
                        el.className = "saved";
                    }
                }
                return response;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    };

    function pushhelper(settingname, vet_fn){
        let el = getEl(settingname);
        let value = el.value;
        return send(`/nvs/${settingname}/`, value, el, vet_fn);

    }

    function buttonclick(ev) {
        let promises = [];
        inputs.forEach((el) => {
            if (el.type != "checkbox") el.className = "";
        })
        inputs.forEach(restoreIfBlank);
        for (let i = 1; i <= NUM_ALARMS; i++) {

            let alarmnum = i;
            let alarmtime_el = getEl("alarmtime" + alarmnum);
            let fadetime_el = getEl("fadetime" + alarmnum);
            let setpoint_el = getEl("setpoint" + alarmnum);
            let enable_el = getEl("enable" + alarmnum);
            let alarmtime = alarmtime_el.value;
            // let fadetime = parseInt(fadetime_el.value, 10) * 1000;
            let setpoint = parseInt(setpoint_el.value, 10);
            let enabled = enable_el.checked ? 1 : 0;

            if (setpoint > 254 || setpoint < 0) {
                console.log("Bad setpoint");
                setpoint_el.classList.add("error");
                return;
            }
            if (alarmtime.length == 0) {
                alarmtime_el.value = "1200";
                alarmtime = "1200";
            }
            else if (alarmtime.length != 4) {
                alarmtime_el.classList.add("error");
                return;
            }
            let hour = parseInt(alarmtime.substring(0, 2), 10);
            let min = parseInt(alarmtime.substring(2, 4), 10);
            if (min > 59 || min < 0) {
                console.log("min not valid");
                alarmtime_el.classList.add("error");
                return;
            }
            if (hour < 0 || hour > 23) {
                console.log("hour not valid");
                alarmtime_el.classList.add("error");
                return;
            }

            send(`/nvs/alarmmin/${alarmnum}/`, min, alarmtime_el);
            send(`/nvs/alarmhour/${alarmnum}/`, hour, alarmtime_el);
            send(`/nvs/alarmfade/${alarmnum}/`, undefined, fadetime_el, (st) => {
                return st * 1000;
            });
            send(`/nvs/alarmsetpoint/${alarmnum}/`, setpoint, setpoint_el);
            send(`/nvs/alarmenable/${alarmnum}/`, enabled, enable_el);
            
            if (i > 5) continue;
            let preset_el = getEl("preset" + i);
            let preset = preset_el.value;
            
            if (preset < 0) {
                preset = 0;
            }
            else if (preset > 254) {
                preset = 254;
            }
            send(`/nvs/preset/${i}/`, preset, preset_el);
        }
        let default_fadetime_el = getEl("default_fadetime");
        let default_fadetime = default_fadetime_el.value;
        if (default_fadetime >= 0 && default_fadetime < (3600 * 10)) {
            promises.push(send("/nvs/default_fade/", undefined, default_fadetime_el,
            (_) => {return _ * 1000;}));
        } else {
            default_fadetime_el.className = "error";
            return;
        }
        let slow_fadetime_el = getEl("slow_fadetime");
        let slow_fadetime = slow_fadetime_el.value;
        if (slow_fadetime >= 0 && slow_fadetime <= (3600 * 10)) {
            promises.push(send("/nvs/slow_fade/", undefined, slow_fadetime_el,
            (_) => {return _ * 1000;}));
        } else {
            slow_fadetime_el.className = "error";
            return;
        }
        
        promises.push(pushhelper("full_power", _ => {return Math.min(Math.max(0, _), 512)}));
        promises.push(pushhelper("lutfile", _ => {return Math.max(0, _)}));
        promises.push(pushhelper("namefile", _ => {return Math.max(0, _)}));
        promises.push(pushhelper("idle_intvl_ms",  _ => {return Math.max(0, _) * 1000}));
        promises.push(pushhelper("idle_cooldown",  _ => {return Math.max(0, _) * 1000}));
        promises.push(pushhelper("startup_level",  _ => {return Math.max(0, _)}));

        for (let i = 0; i < NUM_ALARMS; i++) {
            let channel_el = getEl("dali" + String.fromCharCode(97 + i) + "_address");
            let channel = channel_el.value;
            send("/nvs/" + channel_el.id + "/", channel, channel_el);
        }
        let configbits = 0;
        for (let bit = 0; bit <= 6; bit++) {
            let status = getEl("configbit" + (bit + 1)).checked;
            configbits = configbits | (status << bit);
        }
        if (getEl("configbit1").getAttribute("serverval") != configbits) {
            promises.push(send("/nvs/configbits/", configbits).then(() => {
                for (let bit = 0; bit <= 6; bit++) {
                    let el = getEl("configbit" + (bit + 1));
                    el.setAttribute("serverval", configbits);
                    let label = el.nextElementSibling;
                    label.classList.remove("modified");
                    label.classList.add("saved");
                }
            }));
        }
        Promise.all(promises).then(() => {
            console.log("All done");
            getEl("update1").innerHTML = "Updated!";
            getEl("update2").innerHTML = "Updated!";

        })
    }
    let uri;
    for (let i = 1; i <= NUM_ALARMS; i++) {

        uri = `/nvs/alarmsetpoint/${i}/`;
        get(uri, getEl(`setpoint${i}`));
        uri = `/nvs/alarmfade/${i}/`;
        let fadeel = getEl(`fadetime${i}`);
        get(uri, fadeel, (st) => {
            if (st >= 0) return st * 0.001;
            return ""
        });

        uri = `/nvs/alarmenable/${i}/`;
        get(uri).then((st) => {
            let enable_el = getEl(`enable${i}`);
            enable_el.checked = st == "1";
            enable_el.setAttribute("serverval", st);
        });
        uri = `/nvs/alarmhour/${i}/`;
        let hour;
        let hour_prom = get(uri).then((st) => {
            hour = st;
        });
        uri = `/nvs/alarmmin/${i}/`;
        let min;
        let min_prom = get(uri).then((st) => {
            min = st;
        })
        Promise.all([min_prom, hour_prom]).then(() => {
            let time_el = getEl(`alarmtime${i}`);
            time_el.value = pad(hour) + pad(min);
            time_el.setAttribute("serverval", pad(hour) + pad(min));
        });
        
        if (i>5) continue;
        uri = `/nvs/preset/${i}/`;
        get(uri, getEl("preset" + i));
    }
    let default_fadetime_el = getEl("default_fadetime");
    get("/nvs/default_fade/", default_fadetime_el, (st) => {
        if (st >= 0) return st * 0.001;
        return "";
    });
    let slow_fadetime_el = getEl("slow_fadetime");
    get("/nvs/slow_fade/", slow_fadetime_el, (st) => {
        if (st >= 0){
            return st * 0.001;
        }
        return "";
    })
    get("/nvs/full_power/", getEl("full_power"));
    get("/nvs/lutfile/", getEl("lutfile"));
    get("/nvs/namefile/", getEl("namefile"));
    get("/nvs/startup_level/", getEl("startup_level"));
    get("/nvs/idle_intvl_ms/", getEl("idle_intvl_ms"), st => {
        if (st >= 0){
            return st * 0.001;
        }
        return "";
    });
    get("/nvs/idle_cooldown/", getEl("idle_cooldown"), st => {
        if (st >= 0){
            return st * 0.001;
        }
        return "";
    });

    for (let i = 0; i < 6; i++) {
        let channel_el = getEl("dali" + String.fromCharCode(97 + i) + "_address");
        get("/nvs/" + channel_el.id + "/", channel_el);
    }

    get("/nvs/configbits/").then((bits) => {
        for (let bit = 0; bit <= 6; bit++) {
            let status = (bits >> bit) & 1;
            let configbit_el = getEl("configbit" + (bit + 1));
            configbit_el.checked = status;
            configbit_el.setAttribute("serverval", bits)
        }
    });


    let buttons = Array.from(document.getElementsByTagName("button"));
    buttons.forEach((button_el) => {
        console.log("addedonclick");
        button_el.onclick = buttonclick;
    });

    function clearInput(ev) {
        console.log("cleared");
        let el = ev.srcElement;
        el.setAttribute("serverval", ev.srcElement.value);
        el.value = "";
        inputs.forEach((el_) => {
            if (el_ !== el) restoreIfBlank(el_);
        });

    };
    function markModified(ev) {
        console.log("modified");
        modified = true;

        if (ev.srcElement.type == "checkbox") {
            let label = ev.srcElement.nextElementSibling;
            label.classList.add("modified");
            label.classList.remove("saved");
        } else {
            ev.srcElement.className = "modified";
        }
    };

    function restoreIfBlank(el) {
        // let el = ev.srcElement;
        if (el.value == "") el.value = el.getAttribute("serverval");
    }
    inputs.forEach((input_el) => {
        input_el.onfocus = clearInput;
    });
    inputs.forEach((input_el) => {
        input_el.onchange = markModified;
    });
    inputs.forEach((input_el) => {
        input_el.addEventListener("keypress", function(event) {
            // If the user presses the "Enter" key on the keyboard
            if (event.key === "Enter") {
                document.getElementById("update1").click();
            }
        }) 
    });
}

window.onload = all;