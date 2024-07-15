#define RESPONSE(...) #__VA_ARGS__

static const char* response = RESPONSE(

<!DOCTYPE html>
<html>
<head>
</head>
<body>
    The level is now set to %u
    <div class="slidecontainer">
        <input type="range" min="0" max="254" value="50" class="slider" id="levelslider">
    </div>
    <div>
        Ajax level is <span id="ajaxlevel"></span>
    </div>
    <script>
        var xhttp = new XMLHttpRequest();
        var ontimeout = 0;
        var waiting = 0;
        function send(){
                var val = document.getElementById("levelslider").value;
                document.getElementById('ajaxlevel').innerHTML = val;
                console.log(val);
                xhttp.open("GET", "/level/" + val);
                xhttp.send();
        };
        function onChangeFunction() {
            if (!ontimeout){
                send();
                ontimeout = 1;
                setTimeout(function() {
                    ontimeout = 0;
                    if (waiting){
                        send();
                        waiting = 0;
                    }
                }, 150);
            } else {
                waiting = 1;
            }
        };
        document.getElementById("levelslider").oninput = onChangeFunction;
    </script>
</body>
</html>
);