#pragma once

#include <Lattice/Kernel/Consts.hpp>
#include <Lattice/Kernel/Visibility.hpp>

namespace Lattice {

class NodeSystem;
template<class> class Slot;

class LATTICE_API NodeFactory {
    NodeSystem& nodeSystem_;
    template<class> friend class Slot;

    NodeId createNode(
        NodeId parent,
        std::string_view name,
        BlueprintId bp,
        NodeKind kind,
        uint64_t discriminator = std::numeric_limits<uint64_t>::max()
    );

    void constructObject(NodeId id, BlueprintId implementation);

public:
    NodeFactory(NodeSystem& nodeSystem)
        : nodeSystem_(nodeSystem) {}

    NodeId emplace(NodeId parent, BlueprintId type, std::string_view instance,
                   void (*construct)(void*, NodeId, void*), void* arguments);

    void construct(NodeId id, BlueprintId type,
                   void (*constructor)(void*, NodeId, void*), void* arguments);

    NodeId folder(NodeId parent, std::string_view name);

    void* resolve(NodeId id, BlueprintId api) const;

    NodeId component(
        NodeId parent,
        BlueprintId blueprint,
        std::string_view instance = DefaultInstanceName
    );

    NodeId component(
        NodeId parent,
        std::string_view blueprint,
        std::string_view instance = DefaultInstanceName);
    
    NodeId slot(
        NodeId parent,
        BlueprintId api,
        std::string_view instance = DefaultInstanceName
    );

    NodeId slot(
        NodeId parent, 
        std::string_view api, 
        std::string_view instance = DefaultInstanceName);

    /// False while the previous composed subtree/consumers are retiring; retry after maintain().
    bool choice(
        NodeId id, 
        BlueprintId impl
    );

    NodeId binding(
        NodeId parent,
        std::string_view name
    );

    void share(
        NodeId id, 
        BlueprintId api
    );
};

}
