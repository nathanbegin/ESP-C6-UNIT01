const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;

// Convertisseur custom : mappe chaque endpoint IAS Zone vers son propre contact.
//   Endpoint 1 : On/Off (LED embarquée)
//   Endpoint 2 : IAS Zone -> contact_door1
//   Endpoint 3 : IAS Zone -> contact_door2
//   Endpoint 4 : IAS Zone -> contact_door3
const fzLocal = {
    multi_ias_contact: {
        cluster: 'ssIasZone',
        type: ['commandStatusChangeNotification', 'attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const ep = msg.endpoint.ID;
            const zoneStatus = msg.data.zonestatus !== undefined
                ? msg.data.zonestatus
                : (msg.data.zoneStatus !== undefined ? msg.data.zoneStatus : 0);
            const epName = {2: 'door1', 3: 'door2', 4: 'door3'}[ep];
            if (!epName) return {};
            return {
                // bit0=1 => ouvert => contact false ; convention HA: true = ferme
                [`contact_${epName}`]: (zoneStatus & 1) === 0,
            };
        },
    },
};

module.exports = {
    zigbeeModel: ['ESP-C6-UNIT01'],
    model: 'ESP-C6-UNIT01',
    vendor: 'NathanSensors',
    description: 'ESP32-C6 - LED On/Off + 3 contacts de porte',

    fromZigbee: [fzLocal.multi_ias_contact, fz.on_off],
    toZigbee: [tz.on_off],

    exposes: [
        e.contact().withEndpoint('door1'),
        e.contact().withEndpoint('door2'),
        e.contact().withEndpoint('door3'),
        e.switch().withEndpoint('led'),
    ],

    endpoint: (device) => {
        return {led: 1, door1: 2, door2: 3, door3: 4};
    },
    meta: {multiEndpoint: true},
};