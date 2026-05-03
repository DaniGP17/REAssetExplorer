#ifndef REASSETEXPLORER_FSMDATA_H
#define REASSETEXPLORER_FSMDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Rsz/RszTypes.h"

// A node's children are the states of its FSM; each state lists the transitions to its
// siblings. Actions, conditions and the rest are RSZ objects referenced by block and index.
// Order matches the block offsets in the BHVT header.
enum class FsmBlock : int8_t {
    Action,
    Selector,
    SelectorCaller,
    Condition,
    TransitionEvent,
    ExpressionTreeCondition,
    StaticAction,
    StaticSelectorCaller,
    StaticCondition,
    StaticTransitionEvent,
    StaticExpressionTreeCondition,
    Count
};

struct FsmObjectRef {
    FsmBlock block = FsmBlock::Count;  // Count: none
    int32_t object = -1;               // index into the block's object table

    bool Valid() const { return block != FsmBlock::Count && object >= 0; }
};

// via.behaviortree.NodeAttribute
constexpr uint16_t FSM_NODE_ENABLED = 0x1;
constexpr uint16_t FSM_NODE_RESTARTABLE = 0x2;
constexpr uint16_t FSM_NODE_REFERENCE_TREE = 0x4;
constexpr uint16_t FSM_NODE_BUBBLES_CHILD_END = 0x8;
constexpr uint16_t FSM_NODE_SELECT_ONCE = 0x10;
constexpr uint16_t FSM_NODE_FSM = 0x20;
constexpr uint16_t FSM_NODE_TRAVERSE_TO_LEAF = 0x40;

struct FsmChild {
    int32_t node = -1;  // index into FsmData::nodes
    FsmObjectRef condition;
};

// A transition to another state of the same FSM.
struct FsmState {
    int32_t target = -1;
    FsmObjectRef condition;
    std::vector<FsmObjectRef> events;
    uint32_t transitionMapId = 0;
    int32_t transitionData = -1;  // motion FSMs: index into FsmData::transitions
    uint32_t stateEx = 0;
};

// Which child starts when the node is entered.
struct FsmStartTransition {
    int32_t start = -1;  // -1: none named
    FsmObjectRef condition;
};

// Transitions taken from any child state ("any state").
struct FsmAllState {
    int32_t target = -1;
    FsmObjectRef condition;
    uint32_t transitionMapId = 0;
    int32_t transitionData = -1;
    uint32_t attributes = 0;
};

struct FsmNode {
    uint32_t id = 0;
    uint32_t exId = 0;
    std::string name;
    int32_t parent = -1;
    int32_t priority = 0;
    uint16_t attributes = 0;
    uint16_t workFlags = 0;
    uint32_t nameHash = 0;
    uint32_t fullNameHash = 0;
    std::vector<uint32_t> tags;
    bool isBranch = false;
    bool isEnd = false;
    std::vector<FsmChild> children;
    FsmObjectRef selector;
    std::vector<FsmObjectRef> selectorCallers;
    FsmObjectRef selectorCallerCondition;
    std::vector<FsmObjectRef> actions;
    std::vector<FsmState> states;
    std::vector<FsmStartTransition> transitions;
    std::vector<FsmAllState> allStates;
    std::string referenceTree;
};

// via.motion.TransitionData (motfsm2).
struct FsmTransitionData {
    uint32_t id = 0;
    uint32_t bits = 0;
    float exitFrame = 0;
    float startFrame = 0;
    float interpolationFrame = 0;
    float contOnLayerSpeed = 0;
    float contOnLayerTimeout = 0;
    uint16_t contOnLayerNo = 0;
    uint16_t contOnLayerJointMask = 0;

    uint32_t EndType() const { return bits & 0xF; }
    uint32_t InterpolationMode() const { return (bits >> 4) & 0xF; }
    uint32_t InterpolationCurve() const { return (bits >> 8) & 0xF; }
    uint32_t StartType() const { return (bits >> 13) & 0xF; }
    bool ContOnLayer() const { return (bits >> 18) & 1; }
};

struct FsmData {
    bool motion = false;       // .motfsm2
    uint32_t fileVersion = 0;
    uint32_t treeVersion = 0;
    std::vector<FsmNode> nodes;
    int32_t root = -1;
    std::array<RszData, static_cast<std::size_t>(FsmBlock::Count)> blocks;
    std::vector<FsmTransitionData> transitions;
    std::vector<std::string> resources;
    uint32_t unresolvedActions = 0;

    const RszData& Block(FsmBlock block) const { return blocks[static_cast<std::size_t>(block)]; }
    const RszInstance* Instance(const FsmObjectRef& ref) const;
};

#endif
