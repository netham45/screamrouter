import {callApi} from "./api.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"

let iframeInterval = false;

function iframeOnload() {
    if (iframeInterval != false)
        clearInterval(iframeInterval);
    iframeInterval = setInterval(iframeResize, 1000);
    iframeResize();
    setTimeout(iframeResize, 200);
    setTimeout(iframeResize, 400);
    setTimeout(iframeResize, 600);
    setTimeout(iframeResize, 800);
    setTimeout(iframeResize, 800);
}

function iframeResize() {
    var iframe = document.querySelectorAll("DIV#dialog IFRAME")[0];
    var canvas = iframe.contentWindow.document.getElementsByTagName("canvas")[0];
    if (canvas.width > 0 && canvas.height > 0) {
        iframe.style.width = (canvas.width) + "px";
        iframe.style.height = (canvas.height) + "px";
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