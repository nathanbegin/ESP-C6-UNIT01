import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
const e = exposes.presets;

const endpoints = {led: 1, door1: 2, door2: 3, door3: 4, fan1: 5, fan2: 6, exchange: 7};
const contactNames = {2: 'door1', 3: 'door2', 4: 'door3'};
const switchNames = {1: 'led', 5: 'fan1', 6: 'fan2', 7: 'exchange'};

const contacts = {
    cluster: 'ssIasZone',
    type: ['commandStatusChangeNotification', 'attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const name = contactNames[msg.endpoint.ID];
        const status = msg.data.zonestatus ?? msg.data.zoneStatus;
        if (!name || !Number.isInteger(status) || status < 0 || status > 65535) return {};
        return {[`contact_${name}`]: (status & 1) === 0};
    },
};

const switches = {
    cluster: 'genOnOff',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const name = switchNames[msg.endpoint.ID];
        const value = msg.data.onOff;
        if (!name || ![0, 1, false, true].includes(value)) return {};
        const on = Boolean(value);
        const state = {[`state_${name}`]: on ? 'ON' : 'OFF'};
        // These are exclusive logical speeds, not independent relay coils.
        if (on && name === 'fan1') state.state_fan2 = 'OFF';
        if (on && name === 'fan2') state.state_fan1 = 'OFF';
        return state;
    },
};

const switchCommands = {
    key: ['state'],
    convertSet: async (entity, key, value) => {
        if (!switchNames[entity.ID]) throw new Error('Commande réservée aux endpoints LED/Fan/Échange');
        if (typeof value !== 'string' || !['ON', 'OFF', 'TOGGLE'].includes(value.toUpperCase())) {
            throw new Error('État attendu : ON, OFF ou TOGGLE');
        }
        await entity.command('genOnOff', value.toLowerCase(), {}, {disableDefaultResponse: false});
        // Wait for the firmware report after the switching sequence. Never
        // optimistically show both speeds ON or overwrite a newer report.
        return {};
    },
    convertGet: async (entity) => {
        if (!switchNames[entity.ID]) throw new Error('Endpoint On/Off inconnu');
        await entity.read('genOnOff', ['onOff']);
    },
};

export default {
    zigbeeModel: ['ESP-C6-UNIT01'],
    model: 'ESP-C6-UNIT01',
    vendor: 'NathanSensors',
    description: 'ESP32-C6 - 3 portes, LED, Fan 1/Fan 2 exclusifs et échange extérieur',
    fromZigbee: [contacts, switches],
    toZigbee: [switchCommands],
    exposes: [
        e.contact().withEndpoint('door1'),
        e.contact().withEndpoint('door2'),
        e.contact().withEndpoint('door3'),
        e.switch().withEndpoint('led'),
        e.switch().withEndpoint('fan1').withDescription('Basse vitesse. ON désactive Fan 2; OFF arrête si Fan 1 est actif.'),
        e.switch().withEndpoint('fan2').withDescription('Haute vitesse. ON désactive Fan 1; OFF arrête si Fan 2 est actif.'),
        e.switch().withEndpoint('exchange').withDescription('Ferme SW2 : demande échange extérieur. Indépendant de la vitesse.'),
    ],
    endpoint: () => endpoints,
    meta: {multiEndpoint: true},
    configure: async (device) => {
        // Re-interview/re-pair older firmware devices to discover endpoints 5..7.
        for (const id of [1, 5, 6, 7]) {
            const endpoint = device.getEndpoint(id);
            if (!endpoint) throw new Error(`Endpoint ${id} absent : reflasher puis refaire l'interview Zigbee`);
            await endpoint.read('genOnOff', ['onOff']);
        }
        for (const id of [2, 3, 4]) {
            await device.getEndpoint(id).read('ssIasZone', ['zoneStatus']);
        }
    },
};
