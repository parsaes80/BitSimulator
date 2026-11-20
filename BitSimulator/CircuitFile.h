#pragma once
// Saved circuits: a snapshot of the canvas (and of a compiled design) and its .bitsim JSON form

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <expected>
#include "hdlcompiler.h"   // NetDesign, Items.h

struct SavedComponent {
    ItemProps props;
    QPointF position;
    QString name;            // sources and displays
    bool operator==(const SavedComponent&) const = default;
};

struct SavedWire {
    int from = -1, fromPin = -1;   // component index, index in componentPins()
    int to = -1, toPin = -1;
    bool operator==(const SavedWire&) const = default;
};

struct CircuitData {
    NetDesign design;              // empty for a drawn circuit
    QList<SavedComponent> components;
    QList<SavedWire> wires;
};

inline constexpr int CircuitFileVersion = 1;
inline constexpr const char* CircuitFileSuffix = "bitsim";

// Every pin of a component in a fixed order; absent optional pins stay as null, so indices don't shift
QList<PortItem*> componentPins(ComponentItem* item);

QByteArray circuitToJson(const CircuitData& data);
// Error: why the file can't be read
std::expected<CircuitData, QString> circuitFromJson(const QByteArray& json);
