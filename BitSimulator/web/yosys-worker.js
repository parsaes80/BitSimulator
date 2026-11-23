// YoWASP's yosys off the page's thread; kept between compiles, one run at a time (hdlcompiler.cpp starts them)
import { runYosys } from './yowasp/yosys/gen/bundle.js';

let queue = Promise.resolve();
onmessage = ({ data }) => { queue = queue.then(() => run(data)); };

async function run({ id, args, files }) {
    let log = '';
    const decoder = new TextDecoder();
    const collect = bytes => { if (bytes) log += decoder.decode(bytes, { stream: true }); };
    const fetchProgress = ({ doneLength, totalLength }) => postMessage({ id, download: { done: doneLength, total: totalLength } });
    try {
        const outputs = await runYosys(args, files, { stdout: collect, stderr: collect, fetchProgress });
        postMessage({ id, ok: true, netlist: outputs['code_netlist.json'] ?? null, log });
    } catch (failure) {
        postMessage({ id, ok: false, log: log.trim() || `Yosys could not run: ${failure}` });
    }
}
