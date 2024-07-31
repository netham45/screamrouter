iframe_interval = false;
function iframe_onload() {
    if (iframe_interval != false)
        clearInterval(iframe_interval);
    iframe_interval = setInterval(iframe_resize, 1000);
    iframe_resize();
    setTimeout(iframe_resize, 200);
    setTimeout(iframe_resize, 400);
    setTimeout(iframe_resize, 600);
    setTimeout(iframe_resize, 800);
    setTimeout(iframe_resize, 800);
}

function iframe_resize() {
    var iframe = document.querySelectorAll("DIV#dialog IFRAME")[0];
    var canvas = iframe.contentWindow.document.getElementsByTagName("canvas")[0];
    if (canvas.width > 0 && canvas.height > 0) {
        iframe.style.width = (canvas.width) + "px";
        iframe.style.height = (canvas.height) + "px";
    }
    else
    {
        iframe.style.width = "400px";
        iframe.style.height = "600px";
    }
    if (matchMedia('(pointer:coarse)').matches)
        document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_fullscreen_button").onclick = function() {dialog_cancel();}
    document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_disconnect_button").onclick = function() {dialog_cancel();}
}

function vnc_fullscreen_click() { 
    document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_fullscreen_button").click();
}

addEventListener("fullscreenchange", (event) => {
      if (!document.fullscreenElement && document.getElementsByClassName("vnc-on-click").length > 0 && matchMedia('(pointer:coarse)').matches)
        dialog_cancel();
});