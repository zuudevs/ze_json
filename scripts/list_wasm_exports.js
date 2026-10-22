// Script utilitas: daftar export section file .wasm (tanpa instansiasi).
const fs = require('fs');
const path = require('path');

const file = process.argv[2];
if (!file) {
    console.error('Usage: node list_wasm_exports.js <file.wasm>');
    process.exit(1);
}

const bytes = fs.readFileSync(file);
let i = 8; // lewati magic + version

function readLeb128() {
    let result = 0;
    let shift = 0;
    let byte;
    do {
        byte = bytes[i++];
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    return result;
}

let found = false;
while (i < bytes.length) {
    const sectionId = bytes[i++];
    const size = readLeb128();
    const end = i + size;
    if (sectionId === 7) { // Export section
        found = true;
        const count = readLeb128();
        console.log(`<Export section: ${count} entri>`);
        for (let e = 0; e < count; e++) {
            const nameLen = readLeb128();
            const name = bytes.slice(i, i + nameLen).toString('utf8');
            i += nameLen;
            const kind = bytes[i++]; // 0=func, 1=table, 2=memory, 3=global
            readLeb128(); // index
            const kindName = { 0: 'func', 1: 'table', 2: 'memory', 3: 'global' }[kind];
            console.log(`  [${kindName}] ${name}`);
        }
    }
    i = end;
}

if (!found) console.log('<tidak ada export section>');
