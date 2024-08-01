let optionContainer;
let options;
let selectedOption = null;
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

function onOptionDragPress(event) {
    optionContainer = event.target.parentNode.parentNode;
    options = [...optionContainer.querySelectorAll(":scope > span")];
    selectedOption = event.target.parentNode;
    const rect = selectedOption.getBoundingClientRect();
    mouseOffsetY = rect.top - event.screenY;
}

function onOptionDragMove(event) {
    if (!selectedOption) return;
    const closestOption = getClosestOptionToMouse(event.screenX, event.screenY + mouseOffsetY);
    const rect = closestOption.getBoundingClientRect();
    const closestOptionTop = rect.top;
    if (selectedOption !== closestOption) {
        optionContainer.removeChild(selectedOption);
        event.screenY + mouseOffsetY < closestOptionTop
            ? closestOption.before(selectedOption)
            : closestOption.after(selectedOption);
    }
}

function onOptionDragRelease() {
    if (!selectedOption) return;
    options = [...optionContainer.querySelectorAll(":scope > span")];
    let i=0;
    let name = selectedOption.dataset['name'];
    let type = "";
    for (;i<options.length;i++) {
        if (options[i] == selectedOption) {
            type = options[i].dataset["type"].replace("Description","").toLowerCase();
            call_api(type + "s/" + name + "/reorder/" + i);
            break;
        }
    }
    selectedOption = null;
    mouseOffsetY = 0;
}
