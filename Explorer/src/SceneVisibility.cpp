#include "Explorer/SceneVisibility.h"

#include <memory>
#include <string_view>

namespace {

// Field names differ between the RE7 and RE8 type dumps, so flags are read by position.
bool FlagOff(const RszInstance* instance, std::size_t field) {
    if (instance == nullptr || field >= instance->fields.size()) return false;
    const RszValue& value = instance->fields[field].value;
    if (const bool* b = value.As<bool>()) return !*b;
    if (const RszBytes* bytes = value.As<RszBytes>(); bytes && !bytes->empty()) return (*bytes)[0] == 0;
    return false;
}

bool HasComponent(const SceneData& scene, const SceneNode& node, std::string_view type) {
    for (int32_t index : node.componentIndices) {
        const RszInstance* component = scene.Instance(index);
        if (component && component->typeName == type) return true;
    }
    return false;
}

class SceneVisibilityRule {
public:
    virtual ~SceneVisibilityRule() = default;
    virtual void Apply(const SceneData& scene, std::vector<uint8_t>& hidden) = 0;
};

// via.GameObject: Name, Tag, UpdateSelf, DrawSelf. via.Folder: Name, Tag, then two flags
// that are off on inactive folders (Develop, FSM) and on unused lighting situations.
class DrawFlagsRule : public SceneVisibilityRule {
public:
    void Apply(const SceneData& scene, std::vector<uint8_t>& hidden) override {
        for (std::size_t i = 0; i < scene.nodes.size(); i++) {
            const SceneNode& node = scene.nodes[i];
            const RszInstance* instance = scene.Instance(node.instanceIndex);
            if (node.kind == SceneNode::Kind::GameObject) {
                if (FlagOff(instance, 3)) hidden[i] = 1;
            } else if (FlagOff(instance, 2) || FlagOff(instance, 3)) {
                hidden[i] = 1;
            }
        }
    }
};

// RE8 props that change with the story (burnt houses, broken walls): a behavior tree
// picks one of the Before* / After* children at runtime, and the files keep whichever
// states were left drawn. The game starts before the change.
class ChangeSetRule : public SceneVisibilityRule {
public:
    void Apply(const SceneData& scene, std::vector<uint8_t>& hidden) override {
        for (const SceneNode& node : scene.nodes) {
            if (!HasComponent(scene, node, "app.PropsBehaviorTreeSave") ||
                !HasComponent(scene, node, "via.behaviortree.BehaviorTree")) {
                continue;
            }
            for (std::size_t child : node.children) {
                const std::string& name = scene.nodes[child].name;
                if (name.starts_with("Before")) hidden[child] = 0;
                else if (name.starts_with("After")) hidden[child] = 1;
            }
        }
    }
};

}

std::vector<uint8_t> HiddenByDefault(const SceneData& scene) {
    std::vector<uint8_t> hidden(scene.nodes.size(), 0);
    std::unique_ptr<SceneVisibilityRule> rules[] = { std::make_unique<DrawFlagsRule>(), std::make_unique<ChangeSetRule>() };
    for (const auto& rule : rules) rule->Apply(scene, hidden);
    return hidden;
}
