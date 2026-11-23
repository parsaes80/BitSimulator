// One dot run per worker, with the desktop's arguments; the page ends the worker afterwards, or earlier to stop it
import createDot from './graphviz/dot.js';

onmessage = async ({ data: { graph, args } }) => {
    let messages = '';
    try {
        const dot = await createDot({ print: () => {}, printErr: line => { messages += line + '\n'; } });
        dot.FS.writeFile('graph.dot', graph);
        const status = dot.callMain([...args, '-o', 'layout.plain', 'graph.dot']);
        if (status) throw new Error(`dot failed with exit code ${status}`);
        postMessage({ ok: true, text: dot.FS.readFile('layout.plain', { encoding: 'utf8' }) });
    } catch (failure) {
        postMessage({ ok: false, text: messages.trim() || String(failure) });
    }
};
