const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '../c6_door_sensor.js'), 'utf8');
const modern = fs.readFileSync(path.join(__dirname, '../c6_door_sensor.mjs'), 'utf8');
assert.equal(modern, source.replace("const exposes = require('zigbee-herdsman-converters/lib/exposes');",
    "import * as exposes from 'zigbee-herdsman-converters/lib/exposes';").replace('module.exports = {', 'export default {'));
const expose = () => ({withEndpoint(endpoint) {this.endpoint = endpoint; return this;}, withDescription() {return this;}});
const ctx = {module: {exports: {}}, require: () => ({presets: {contact: expose, switch: expose}})};
vm.runInNewContext(source, ctx);
const definition = ctx.module.exports;
assert.equal(definition.ota, true);
const plain = (x) => JSON.parse(JSON.stringify(x));
const contacts = definition.fromZigbee[0].convert;
const switches = definition.fromZigbee[1].convert;

for (let ep = 2; ep <= 4; ++ep) {
    for (const field of ['zonestatus', 'zoneStatus']) {
        for (const value of [0, 1, 2, 3]) {
            assert.equal(contacts(null, {endpoint: {ID: ep}, data: {[field]: value}})[`contact_door${ep - 1}`], !(value & 1));
        }
    }
    for (const data of [{}, {zonetype: 21}, {zoneStatus: null}, {zoneStatus: '0'}, {zoneStatus: -1}]) {
        assert.deepEqual(plain(contacts(null, {endpoint: {ID: ep}, data})), {});
    }
}

assert.deepEqual(plain(switches(null, {endpoint: {ID: 5}, data: {onOff: 1}})),
    {state_fan1: 'ON', state_fan2: 'OFF', state_exchange: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 6}, data: {onOff: 1}})),
    {state_fan2: 'ON', state_fan1: 'OFF', state_exchange: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 5}, data: {onOff: 0}})),
    {state_fan1: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 6}, data: {onOff: 0}})),
    {state_fan2: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 7}, data: {onOff: 1}})),
    {state_exchange: 'ON', state_fan1: 'OFF', state_fan2: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 7}, data: {onOff: 0}})),
    {state_exchange: 'OFF'});
assert.deepEqual(plain(switches(null, {endpoint: {ID: 7}, data: {}})), {});
assert.equal(definition.exposes.filter((e) => ['fan1', 'fan2', 'exchange'].includes(e.endpoint)).length, 3);

(async () => {
    const calls = [];
    const entity = {ID: 5, command: async (...args) => calls.push(args), read: async (...args) => calls.push(args)};
    for (const value of ['ON', 'OFF', 'TOGGLE']) {
        assert.deepEqual(plain(await definition.toZigbee[0].convertSet(entity, 'state', value)), {});
        assert.equal(calls.at(-1)[1], value.toLowerCase());
    }
    await assert.rejects(() => definition.toZigbee[0].convertSet(entity, 'state', 'invalid'));
    await assert.rejects(() => definition.toZigbee[0].convertSet({...entity, ID: 2}, 'state', 'ON'));
    await definition.configure({getEndpoint: (ID) => ({...entity, ID})});
    await assert.rejects(() => definition.configure({getEndpoint: () => undefined}));
    console.log('PASS: contacts, Low/High/Exchange exclusifs, non-optimistic commands, configure, CJS/ESM parity');
})().catch((error) => {console.error(error); process.exitCode = 1;});
