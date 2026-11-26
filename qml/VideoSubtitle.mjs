// Qt's FFmpeg backend delivers dialogue with inline ASS overrides, including
// for SubRip tracks. Only markup produced here is passed to Qt StyledText.
const emphasis = ["b", "i", "u", "s"]

function defaultStyle() {
    return { b: false, i: false, u: false, s: false, color: "", drawing: false }
}

function applyOverride(command, style, cue) {
    const weightOrEmphasis = /^([bius])\s*(-?\d+)?$/.exec(command)
    if (weightOrEmphasis) {
        const tag = weightOrEmphasis[1]
        const value = Number(weightOrEmphasis[2] || 0)
        if (tag === "b")
            cue.explicitWeight = true
        style[tag] = tag === "b" ? value === 1 || value === -1 || value >= 600 : value !== 0
        return
    }
    if (command.startsWith("r")) {
        // Qt delivers dialogue without the track's named style table.
        Object.assign(style, defaultStyle())
        return
    }
    const alignment = /^an([1-9])$/.exec(command)
    if (alignment && !cue.alignment)
        cue.alignment = Number(alignment[1])
    const drawing = /^p(\d+)$/.exec(command)
    if (drawing)
        style.drawing = Number(drawing[1]) > 0
    const primary = /^1?c(?:&H([0-9a-f]{1,8})&?)?$/i.exec(command)
    if (primary) {
        const bgr = ("000000" + (primary[1] || "")).slice(-6)
        style.color = primary[1] ? "#" + bgr.slice(4) + bgr.slice(2, 4) + bgr.slice(0, 2) : ""
    }
}

function applyBlock(block, style, cue) {
    // A transform or clip may contain more commands in parentheses. Those
    // belong to the enclosing effect and must not change the static text.
    let depth = 0
    let start = block.indexOf("\\") + 1
    for (let i = start; i <= block.length; ++i) {
        if (i === block.length || (block[i] === "\\" && depth === 0)) {
            applyOverride(block.slice(start, i).trim(), style, cue)
            start = i + 1
        } else if (block[i] === "(") {
            ++depth
        } else if (block[i] === ")" && depth > 0) {
            --depth
        }
    }
}

function styledRun(text, style, displayColor) {
    if (style.drawing || !text)
        return ""
    // Character references keep literal dialogue and its whitespace intact
    // in StyledText. Regular spaces still wrap, while ASS hard spaces do not.
    let run = text.replace(/[&<>\s]/g, ch => "&#" + ch.charCodeAt(0) + ";")
    for (const tag of emphasis) {
        if (style[tag])
            run = "<" + tag + ">" + run + "</" + tag + ">"
    }
    if (style.color)
        run = '<font color="' + displayColor(style.color) + '">' + run + "</font>"
    return run
}

export function format(source, displayColor) {
    const cue = { text: "", alignment: 0, explicitWeight: false }
    const style = defaultStyle()
    const tokens = /\{[ \t]*\\[^{}\r\n]*\}|\\[Nnh{}\\]|\r\n|[\r\n\u2028\u2029]/g
    let start = 0
    let match
    while ((match = tokens.exec(source)) !== null) {
        cue.text += styledRun(source.slice(start, match.index), style, displayColor)
        const token = match[0]
        if (token[0] === "{")
            applyBlock(token.slice(1, -1), style, cue)
        else if (token === "\\h")
            cue.text += styledRun("\u00a0", style, displayColor)
        else if (token === "\\{" || token === "\\}" || token === "\\\\")
            cue.text += styledRun(token[1], style, displayColor)
        else if (!style.drawing)
            cue.text += "<br>"
        start = tokens.lastIndex
    }
    cue.text += styledRun(source.slice(start), style, displayColor)
    cue.alignment = cue.alignment || 2 // ASS uses the numeric keypad positions.
    return cue
}
