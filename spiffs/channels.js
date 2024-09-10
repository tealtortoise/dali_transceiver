
"use strict";
function all() {
    function get(uri, el) {
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
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    }
    var ontimeout = 0;
    var waiting = 0;
    let slider = document.getElementById("levelslider");
    let levelind = document.getElementById('ajaxlevel');

    function send(el, value){
        if (value == null){
            value = el.value;
        }
        const options = {
        method: 'PUT',
        headers: {
            'Content-Type': 'application/plain'
        },
        body: `${value}`
        };

        let uri = `/api/channel/${el.id}/`;
        // Make the PUT request using the fetch API
        return fetch(uri, options)
            .then(response => {
                if (!response.ok) {
                throw new Error('Network response was not ok');
                }
                return response;
            })
            .catch(error => {
                console.error('There was a problem with your fetch operation:', error);
            });
    };

    function getChecked(slider_el){
        let id = slider_el.id;
        let enable_el = document.getElementById(`en_${id}`);
        return enable_el.checked;
    }

    let checked_ob = {};
    document.checked_ob = checked_ob;
    Array.from(document.getElementsByClassName("lvlslider")).forEach((el) => {  
        let id = el.id;
        checked_ob[id] = getChecked(el);
        get(`/api/channel/${id}/`).then((res) => {
            if (res == -1) {
                document.getElementById(`en_${id}`).checked = false;
                checked_ob[id] = false;
                el.value = 0;
                let level_ind = document.getElementById(id + "_level");
                level_ind.innerHTML = "0";
            } else {
                document.getElementById(`en_${id}`).checked = true;
                checked_ob[id] = true;
                el.value = res;
                let level_ind = document.getElementById(id + "_level");
                level_ind.innerHTML = res;
            }
        });
        if (id.substring(0, 4) == "dali") {
            get(`/nvs/${id.substring(0,5)}_address/`).then((resp) => {
                let ch_ind_id = `${id}_ch`;
                let ch_el = document.getElementById(ch_ind_id);
                if (resp == -1){
                    ch_el.innerHTML = `Addr ${resp} (Off)`;
                } else if (resp == 200) {
                    ch_el.innerHTML = `Addr ${resp} (Brdcast)`;
                } else if (resp >= 100 && resp <= 115) {
                    ch_el.innerHTML = `Addr ${resp} (Grp ${resp-100})`;
                } else if (resp >= 0 && resp <= 63) {
                    ch_el.innerHTML = `Addr ${resp}`;
                } else {
                    ch_el.innerHTML = `Unknown Addr ${resp}`;
                }
            });
        }
    });

    Array.from(document.getElementsByClassName("input")).forEach((el) => {
        el.onchange = (ev) => {
            let id = ev.srcElement.id;
            let checked = ev.srcElement.checked;
            checked_ob[id.substring(3)] = checked;
            let slider_el = document.getElementById(id.substring(3));
            send(slider_el, checked ? slider_el.value : -1);
        };
    });


    function sliderChangeFn(ev) {
        let slider_el = ev.srcElement;
        let sliderval = slider_el.value;
        let id = slider_el.id;
        let level_ind = document.getElementById(id + "_level");
        level_ind.innerHTML = sliderval;
        // levelind.classList.add("updating");
        if (!ontimeout) {
            if (checked_ob[id]) send(slider_el);
            ontimeout = 1;
            setTimeout(() => {
                if (waiting) {
                    if (checked_ob[id]) send(slider_el);
                    waiting = 0;
                }
                ontimeout = 0;
            }, 120);
        } else {
            waiting = 1;
        }
    };
    Array.from(document.getElementsByClassName("lvlslider")).forEach((el) => {
        el.oninput = sliderChangeFn;
    });
}
window.onload = all;