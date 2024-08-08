import {callApi as callApi} from "./api.js"
import {drawLines as drawLines} from "./lines.js"
import { getRouteBySinkSource, exposeFunction } from "./utils.js"
import {nullCallback, restartCallback, editSourceCallback, editSinkCallback, restartCallback2} from "./main.js"

let optionContainer;
let options;
let selectedOption = null;
let selectedOptionKeyboard = null;
let mouseOffsetY = 0;

function getClosestOptionToMouse(mouseX, mouseY) {
    return options.reduce((closest, option) => {
        const rect = option.getBoundingClientRect();
        const optionCenterX = rect.left + rect.width / 2;
        const optionCenterY = rect.top + rect.height / 2;
        const distance = Math.sqrt(Math.pow(optionCenterX - mouseX, 2) + Math.pow(optionCenterY - mouseY, 2));

        return distance < closest.distance ? { option, distance } : closest;
    }, { option: null, distance: Infinity }).option;
}

function setupOptionDrag(event, touchEvent = false) {
    optionContainer = event.target.parentNode.parentNode;
    options = [...optionContainer.querySelectorAll(":scope > span")];
    selectedOption = event.target.parentNode;
    const rect = selectedOption.getBoundingClientRect();
    mouseOffsetY = rect.top - (touchEvent ? event.changedTouches[0].screenY : event.screenY);
}

function moveSelectedOption(event, touchEvent = false) {
    if (!selectedOption) return;
    event.preventDefault();
    const screenX = touchEvent ? event.changedTouches[0].screenX : event.screenX;
    const screenY = touchEvent ? event.changedTouches[0].screenY : event.screenY;
    const closestOption = getClosestOptionToMouse(screenX, screenY + mouseOffsetY);
    const rect = closestOption.getBoundingClientRect();
    const closestOptionTop = rect.top;
    if (selectedOption !== closestOption) {
        optionContainer.removeChild(selectedOption);
        screenY + mouseOffsetY < closestOptionTop
            ? closestOption.before(selectedOption)
            : closestOption.after(selectedOption);
    }
}

function onOptionDragStart(event) {
    event.preventDefault();
    setupOptionDrag(event);
}

function onOptionDragTouchStart(event) {
    event.preventDefault();
    setupOptionDrag(event, true);
}

function onOptionDragMove(event) {
    moveSelectedOption(event);
}

function onOptionDragTouchMove(event) {
    moveSelectedOption(event, true);
}

function onOptionDragEnd(event) {
    if (!selectedOption) return;
    event.preventDefault();
    options = [...optionContainer.querySelectorAll(":scope > span")];
    const index = options.findIndex(option => option === selectedOption);
    const name = selectedOption.dataset['name'];
    const type = selectedOption.dataset["type"].replace("Description", "").toLowerCase();
    callApi(`${type}s/${name}/reorder/${index}`);
    selectedOption = null;
    mouseOffsetY = 0;
    drawLines();
}

function onOptionDragKeyDown(event) {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault(); 

    if (selectedOptionKeyboard !== event.target.parentNode) {
        optionContainer = event.target.parentNode.parentNode;
        options = [...optionContainer.querySelectorAll(":scope > span")];
        selectedOptionKeyboard = event.target.parentNode;
    }

    const index = options.findIndex(option => option === selectedOptionKeyboard);
    if (index === -1) return;

    const newIndex = event.key === "ArrowUp" ? index - 1 : index + 1;
    if (newIndex < 0 || newIndex >= options.length) return;

    const type = options[newIndex].dataset["type"].replace("Description", "").toLowerCase();
    const name = selectedOptionKeyboard.dataset["name"];
    callApi(`${type}s/${name}/reorder/${newIndex}`, "get", {}, restartCallback);
}

export function onload() {
    exposeFunction(onOptionDragStart, "onOptionDragStart");
    exposeFunction(onOptionDragTouchStart, "onOptionDragTouchStart");
    exposeFunction(onOptionDragMove, "onOptionDragMove");
    exposeFunction(onOptionDragTouchMove, "onOptionDragTouchMove");
    exposeFunction(onOptionDragEnd, "onOptionDragEnd");
    exposeFunction(onOptionDragKeyDown, "onOptionDragKeyDown");
}