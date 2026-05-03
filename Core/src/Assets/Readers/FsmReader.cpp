#include "Core/Assets/Readers/FsmReader.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

#include "Core/IO/MemoryReader.h"
#include "Core/Rsz/RszReader.h"

namespace {

constexpr uint32_t MOTFSM2_MAGIC = 0x3273666D;  // "mfs2"
constexpr uint32_t BHVT_MAGIC = 0x54564842;     // "BHVT"
constexpr uint32_t NO_NODE = 0xFFFFFFFF;
constexpr int32_t MAX_COUNT = 1024;
constexpr uint8_t STATIC_ID = 64;

int32_t Count(MemoryReader& r, int32_t limit = MAX_COUNT) {
    int32_t count = r.Read<int32_t>();
    if (count < 0 || count > limit) throw std::runtime_error("bad BHVT count");
    return count;
}

// Column-major uint[fields, count]: every value of the first field, then of the second...
std::vector<uint32_t> Columns(MemoryReader& r, int fields, int32_t count) {
    std::vector<uint32_t> data(static_cast<std::size_t>(fields) * count);
    for (uint32_t& v : data) v = r.Read<uint32_t>();
    return data;
}

// BHVTId: object index in the low 16 bits, 64 in the top byte for the static blocks.
FsmObjectRef ObjectRef(uint32_t id, FsmBlock dynamic, FsmBlock staticBlock) {
    FsmObjectRef ref;
    if ((id & 0xFFFF) == 0xFFFF) return ref;
    ref.block = (id >> 24) == STATIC_ID ? staticBlock : dynamic;
    ref.object = static_cast<int32_t>(id & 0xFFFF);
    return ref;
}

FsmObjectRef Condition(uint32_t id) {
    return ObjectRef(id, FsmBlock::Condition, FsmBlock::StaticCondition);
}

std::string WideStringAt(std::span<const uint8_t> data, std::size_t offset, std::size_t* end = nullptr) {
    std::string text;
    uint16_t pendingHigh = 0;
    std::size_t at = offset;
    for (; at + 1 < data.size(); at += 2) {
        uint16_t unit = static_cast<uint16_t>(data[at] | data[at + 1] << 8);
        if (unit == 0) break;
        MemoryReader::AppendUtf16(text, unit, pendingHigh);
    }
    if (end) *end = at + 2;
    return text;
}

struct RawNode {
    uint32_t parentId = NO_NODE;
    uint32_t parentExId = 0;
    int32_t nameOffset = 0;
    int32_t selectorId = -1;
    std::vector<std::pair<uint32_t, uint32_t>> childIds;
    std::vector<std::pair<uint32_t, uint32_t>> stateTargets;
    std::vector<uint32_t> startIds;
    std::vector<std::pair<uint32_t, uint32_t>> allStateTargets;
    std::vector<uint32_t> actionIds;
    int32_t referenceTreeOffset = -1;
};

void ReadNode(MemoryReader& r, uint32_t treeVersion, FsmNode& node, RawNode& raw) {
    node.id = r.Read<uint32_t>();
    node.exId = r.Read<uint32_t>();
    raw.nameOffset = r.Read<int32_t>();
    raw.parentId = r.Read<uint32_t>();
    raw.parentExId = r.Read<uint32_t>();

    int32_t children = Count(r);
    std::vector<uint32_t> childData = Columns(r, 3, children);
    for (int32_t i = 0; i < children; i++) {
        raw.childIds.emplace_back(childData[i], childData[children + i]);
        node.children.push_back({ -1, Condition(childData[2 * children + i]) });
    }

    raw.selectorId = r.Read<int32_t>();
    int32_t callers = Count(r);
    for (int32_t i = 0; i < callers; i++) {
        node.selectorCallers.push_back(ObjectRef(r.Read<uint32_t>(), FsmBlock::SelectorCaller, FsmBlock::StaticSelectorCaller));
    }
    node.selectorCallerCondition = Condition(r.Read<uint32_t>());

    int32_t actions = Count(r);
    std::vector<uint32_t> actionData = Columns(r, 2, actions);
    raw.actionIds.assign(actionData.begin(), actionData.begin() + actions);
    node.priority = r.Read<int32_t>();

    node.attributes = r.Read<uint16_t>();
    if (node.attributes & FSM_NODE_FSM) {
        node.workFlags = r.Read<uint16_t>();
        node.nameHash = r.Read<uint32_t>();
        node.fullNameHash = r.Read<uint32_t>();
        int32_t tags = Count(r);
        for (int32_t i = 0; i < tags; i++) node.tags.push_back(r.Read<uint32_t>());
        node.isBranch = r.Read<uint8_t>() != 0;
        node.isEnd = r.Read<uint8_t>() != 0;
    } else {
        r.Skip(2);
    }

    int32_t states = Count(r);
    std::vector<std::vector<uint32_t>> eventIds(static_cast<std::size_t>(states));
    for (int32_t i = 0; i < states; i++) {
        int32_t events = Count(r);
        for (int32_t e = 0; e < events; e++) eventIds[i].push_back(r.Read<uint32_t>());
    }
    std::vector<uint32_t> stateData = Columns(r, 5, states);
    for (int32_t i = 0; i < states; i++) {
        FsmState state;
        raw.stateTargets.emplace_back(stateData[i], stateData[3 * states + i]);
        state.condition = Condition(stateData[states + i]);
        state.transitionMapId = stateData[2 * states + i];
        state.stateEx = stateData[4 * states + i];
        for (uint32_t id : eventIds[i]) {
            state.events.push_back(ObjectRef(id, FsmBlock::TransitionEvent, FsmBlock::StaticTransitionEvent));
        }
        node.states.push_back(std::move(state));
    }

    int32_t transitions = Count(r);
    // Games whose RSZ has user data (RE2 on) prefix each start transition with its event ids.
    if (treeVersion >= 30) {
        for (int32_t i = 0; i < transitions; i++) r.Skip(static_cast<std::size_t>(Count(r)) * 4);
    }
    std::vector<uint32_t> transitionData = Columns(r, 3, transitions);
    for (int32_t i = 0; i < transitions; i++) {
        raw.startIds.push_back(transitionData[i]);
        node.transitions.push_back({ -1, Condition(transitionData[transitions + i]) });
    }

    if ((node.attributes & FSM_NODE_REFERENCE_TREE) == 0) {
        int32_t allStates = Count(r);
        std::vector<uint32_t> allData = Columns(r, 5, allStates);
        for (int32_t i = 0; i < allStates; i++) {
            FsmAllState state;
            raw.allStateTargets.emplace_back(allData[i], allData[3 * allStates + i]);
            state.condition = Condition(allData[allStates + i]);
            state.transitionMapId = allData[2 * allStates + i];
            state.attributes = allData[4 * allStates + i];
            node.allStates.push_back(state);
        }
    }
    raw.referenceTreeOffset = r.Read<int32_t>();
}

uint32_t ActionId(const RszInstance& action) {
    // Every action type starts with via.behaviortree.Action: v0 flags, v1 id.
    if (action.fields.size() < 2) return 0;
    const RszValue& value = action.fields[1].value;
    if (const RszBytes* bytes = value.As<RszBytes>(); bytes && bytes->size() >= 4) {
        uint32_t id;
        std::memcpy(&id, bytes->data(), 4);
        return id;
    }
    if (const int64_t* v = value.As<int64_t>()) return static_cast<uint32_t>(*v);
    if (const uint64_t* v = value.As<uint64_t>()) return static_cast<uint32_t>(*v);
    return 0;
}

void ReadTree(std::span<const uint8_t> data, std::size_t base, uint32_t treeVersion, const RszTypeDatabase& db,
              FsmData& fsm) {
    MemoryReader r(data);
    r.Seek(base);
    if (r.Read<uint32_t>() != BHVT_MAGIC) throw std::runtime_error("not a BHVT tree");
    r.Skip(4);  // hash
    if (treeVersion >= 42) r.Skip(4);
    uint64_t nodeOffset = r.Read<uint64_t>();
    std::array<uint64_t, static_cast<std::size_t>(FsmBlock::Count)> blockOffsets{};
    for (uint64_t& offset : blockOffsets) offset = r.Read<uint64_t>();
    uint64_t stringOffset = r.Read<uint64_t>();
    uint64_t resourceOffset = r.Read<uint64_t>();

    for (std::size_t b = 0; b < blockOffsets.size(); b++) {
        if (blockOffsets[b] == 0) continue;
        fsm.blocks[b] = RszReader(db).Read(r, base + blockOffsets[b]);
    }

    r.Seek(base + nodeOffset);
    int32_t nodeCount = Count(r, 1 << 20);
    std::vector<RawNode> raw(static_cast<std::size_t>(nodeCount));
    fsm.nodes.resize(static_cast<std::size_t>(nodeCount));
    for (int32_t i = 0; i < nodeCount; i++) ReadNode(r, treeVersion, fsm.nodes[i], raw[i]);

    // Name pool: u32 length, then null-terminated UTF-16 strings.
    for (std::size_t i = 0; i < fsm.nodes.size(); i++) {
        fsm.nodes[i].name = WideStringAt(data, base + stringOffset + 4 + static_cast<std::size_t>(raw[i].nameOffset) * 2);
    }
    // Resources: count, pool length, then the paths.
    if (resourceOffset > 0 && base + resourceOffset + 8 <= data.size()) {
        MemoryReader pool(data);
        pool.Seek(base + resourceOffset);
        int32_t count = pool.Read<int32_t>();
        std::size_t at = base + resourceOffset + 8;
        for (int32_t i = 0; i < count && at < data.size() && i < MAX_COUNT * 16; i++) {
            fsm.resources.push_back(WideStringAt(data, at, &at));
        }
    }

    std::map<std::pair<uint32_t, uint32_t>, int32_t> byId;
    std::unordered_map<uint32_t, int32_t> byIdOnly;
    for (int32_t i = 0; i < nodeCount; i++) {
        byId.emplace(std::make_pair(fsm.nodes[i].id, fsm.nodes[i].exId), i);
        byIdOnly.emplace(fsm.nodes[i].id, i);
    }
    auto find = [&](std::pair<uint32_t, uint32_t> id) {
        if (auto it = byId.find(id); it != byId.end()) return it->second;
        if (auto it = byIdOnly.find(id.first); it != byIdOnly.end()) return it->second;
        return -1;
    };

    // Actions match their instance by id, in order: ids repeat across nodes
    // and each node takes the next instance with its id.
    std::unordered_map<uint32_t, std::vector<FsmObjectRef>> actionsById;
    for (FsmBlock block : { FsmBlock::Action, FsmBlock::StaticAction }) {
        const RszData& rsz = fsm.Block(block);
        for (std::size_t o = 0; o < rsz.objectTable.size(); o++) {
            FsmObjectRef ref{ block, static_cast<int32_t>(o) };
            if (const RszInstance* instance = fsm.Instance(ref)) actionsById[ActionId(*instance)].push_back(ref);
        }
    }
    std::unordered_map<uint32_t, std::size_t> actionsTaken;

    for (int32_t i = 0; i < nodeCount; i++) {
        FsmNode& node = fsm.nodes[i];
        const RawNode& ids = raw[i];
        if (ids.parentId == NO_NODE) {
            if (fsm.root < 0) fsm.root = i;
        } else {
            node.parent = find({ ids.parentId, ids.parentExId });
        }
        for (std::size_t c = 0; c < node.children.size(); c++) node.children[c].node = find(ids.childIds[c]);
        if (ids.selectorId >= 0) node.selector = { FsmBlock::Selector, ids.selectorId };
        for (std::size_t s = 0; s < node.states.size(); s++) node.states[s].target = find(ids.stateTargets[s]);
        for (std::size_t s = 0; s < node.allStates.size(); s++) node.allStates[s].target = find(ids.allStateTargets[s]);
        for (std::size_t t = 0; t < node.transitions.size(); t++) {
            if (ids.startIds[t] == NO_NODE) continue;
            for (std::size_t c = 0; c < node.children.size(); c++) {
                if (ids.childIds[c].first == ids.startIds[t]) node.transitions[t].start = node.children[c].node;
            }
        }
        for (uint32_t id : ids.actionIds) {
            auto it = actionsById.find(id);
            if (it == actionsById.end()) {
                fsm.unresolvedActions++;
                node.actions.push_back({});
                continue;
            }
            std::size_t& taken = actionsTaken[id];
            node.actions.push_back(it->second[std::min(taken, it->second.size() - 1)]);
            taken++;
        }
        if ((node.attributes & FSM_NODE_REFERENCE_TREE) && ids.referenceTreeOffset >= 0) {
            node.referenceTree = WideStringAt(data, base + resourceOffset + 8 + static_cast<std::size_t>(ids.referenceTreeOffset) * 2);
        }
    }
}

}

const RszInstance* FsmData::Instance(const FsmObjectRef& ref) const {
    if (!ref.Valid()) return nullptr;
    const RszData& rsz = Block(ref.block);
    if (static_cast<std::size_t>(ref.object) >= rsz.objectTable.size()) return nullptr;
    int32_t index = rsz.objectTable[static_cast<std::size_t>(ref.object)];
    return index >= 0 && static_cast<std::size_t>(index) < rsz.instances.size() ? &rsz.instances[index] : nullptr;
}

bool FsmReader::SupportsPath(std::string_view path) const {
    return path.find(".motfsm2.") != std::string_view::npos || path.find(".fsmv2.") != std::string_view::npos;
}

FsmData FsmReader::Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const {
    if (data.size() < 16) throw std::runtime_error("FSM file too small");
    MemoryReader r(data);
    FsmData fsm;
    uint32_t first = r.Read<uint32_t>();
    uint32_t second = r.Read<uint32_t>();
    if (second == MOTFSM2_MAGIC) {
        fsm.motion = true;
        fsm.fileVersion = first;
        // motfsm2 versions run ahead of the tree they hold (REE-Lib ToBhvtVersion).
        fsm.treeVersion = first >= 45 ? first - 3 : first >= 36 ? first - 2 : first >= 31 ? first - 1 : first;
        r.Skip(8);
        uint64_t treeOffset = r.Read<uint64_t>();
        uint64_t mapOffset = r.Read<uint64_t>();
        uint64_t dataOffset = r.Read<uint64_t>();
        if (first >= 45) r.Skip(8);
        r.Skip(8);  // tree size pointer
        int32_t mapCount = Count(r, 1 << 20);
        int32_t dataCount = Count(r, 1 << 20);

        r.Seek(mapOffset);
        std::unordered_map<uint32_t, int32_t> maps;
        for (int32_t i = 0; i < mapCount; i++) {
            uint32_t id = r.Read<uint32_t>();
            maps[id] = r.Read<int32_t>();
        }
        r.Seek(dataOffset);
        for (int32_t i = 0; i < dataCount; i++) {
            FsmTransitionData t;
            t.id = r.Read<uint32_t>();
            t.bits = r.Read<uint32_t>();
            t.exitFrame = r.Read<float>();
            if (first >= 45) r.Skip(4);
            t.startFrame = r.Read<float>();
            t.interpolationFrame = r.Read<float>();
            if (first > 31) {
                t.contOnLayerSpeed = r.Read<float>();
                t.contOnLayerTimeout = r.Read<float>();
                t.contOnLayerNo = r.Read<uint16_t>();
                t.contOnLayerJointMask = r.Read<uint16_t>();
            }
            r.Skip(4);
            fsm.transitions.push_back(t);
        }

        ReadTree(data, static_cast<std::size_t>(treeOffset), fsm.treeVersion, db, fsm);
        auto dataIndex = [&](uint32_t mapId) {
            auto it = maps.find(mapId);
            return mapId != 0 && it != maps.end() && it->second >= 0 && it->second < dataCount ? it->second : -1;
        };
        for (FsmNode& node : fsm.nodes) {
            for (FsmState& state : node.states) state.transitionData = dataIndex(state.transitionMapId);
            for (FsmAllState& state : node.allStates) state.transitionData = dataIndex(state.transitionMapId);
        }
    } else if (first == BHVT_MAGIC) {
        fsm.treeVersion = fsmv2TreeVersion;
        fsm.fileVersion = fsmv2TreeVersion;
        ReadTree(data, 0, fsm.treeVersion, db, fsm);
    } else {
        throw std::runtime_error("not an FSM (no mfs2 or BHVT magic)");
    }
    return fsm;
}
