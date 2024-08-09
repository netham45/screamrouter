import {callApi} from "./api.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"

let iframeInterval = false;

function iframeOnload() {
    if (iframeInterval != false)
        clearInterval(iframeInterval);
    iframeInterval = setInterval(iframeOnresize, 1000);
    iframeOnresize();
    setTimeout(iframeOnresize, 200);
    setTimeout(iframeOnresize, 400);
    setTimeout(iframeOnresize, 600);
    setTimeout(iframeOnresize, 800);
    setTimeout(iframeOnresize, 800);
}

export function iframeOnresize() {
    var iframe = document.querySelectorAll("DIV#dialog IFRAME")[0];
    var canvas = iframe.contentWindow.document.getElementsByTagName("canvas")[0];
    var maxWidth = window.innerWidth * 0.75;
    var maxHeight = window.innerHeight * 0.75;
    var width = canvas.width < maxWidth ? canvas.width : maxWidth;
    var height = canvas.height < maxHeight? canvas.height : maxHeight;
    if (canvas.width > 0 && canvas.height > 0) {
        iframe.style.width = width + "px";
        iframe.style.height = height + "px";
    } else {
        iframe.style.width = "400px";
        iframe.style.height = "600px";
    }
    if (matchMedia('(pointer:coarse)').matches)
        document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_fullscreen_button").onclick = function() {dialogCancel();}
    document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_disconnect_button").onclick = function() {dialogCancel();}
}

function vncFullscreenClick() { 
    document.getElementById("vnc-iframe").contentDocument.getElementById("noVNC_fullscreen_button").click();
}

export function onload() {
    exposeFunction(iframeOnload, "iframeOnload");
    exposeFunction(vncFullscreenClick, "vncFullscreenClick");
    addEventListener("fullscreenchange", (event) => {
        if (!document.fullscreenElement && document.getElementsByClassName("vnc-on-click").length > 0 && matchMedia('(pointer:coarse)').matches)
          dialogCancel();
    });
}