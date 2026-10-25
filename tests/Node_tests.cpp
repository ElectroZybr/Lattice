#include <Lattice/tests/RuntimeFixture.hpp>
#include <lattice-test/Tests.hpp>

#include <Lattice/Lattice.hpp>


namespace Lattice {

struct BasicTestObject {
    int value = 0;
};

TEST(Node_Emplace, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    REQUIRE(!root.find<BasicTestObject>().exists());

    root.emplace<BasicTestObject>("");

    REQUIRE(root.find<BasicTestObject>().exists());
    REQUIRE(nodes.registry.require(nodes.query.require(fixture.root, typeKey<BasicTestObject>())).name.empty());
    REQUIRE(root.require<BasicTestObject>().exists());
}

TEST(Node_EmplaceDuplicateRejected, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    root.emplace<BasicTestObject>("same");

    bool thrown = false;
    try {
        root.emplace<BasicTestObject>("same");
    } catch (const ExceptionBase&) {
        thrown = true;
    }

    REQUIRE(thrown);
    REQUIRE(nodes.query.collect(fixture.root, typeKey<BasicTestObject>()).size() == 1);
}

TEST(Node_InstanceIsolation, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    root.emplace<BasicTestObject>("first");
    root.emplace<BasicTestObject>("second");

    REQUIRE(root.find<BasicTestObject>("first").exists());
    REQUIRE(root.find<BasicTestObject>("second").exists());
    REQUIRE(!root.find<BasicTestObject>("default").exists());
    REQUIRE(nodes.query.collect(fixture.root, typeKey<BasicTestObject>()).size() == 2);
}

TEST(Node_RequireMissing, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<BasicTestObject>("other");
    bool thrown = false;

    try {
        root.require<BasicTestObject>();
    } catch (const ExceptionBase&) {
        thrown = true;
    }

    REQUIRE(thrown);
}

TEST(Node_ParentLookup, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<BasicTestObject>("");
    auto branch = root.branch("branch");

    REQUIRE(branch.find<BasicTestObject>().exists());
    REQUIRE(branch.require<BasicTestObject>().exists());
}

TEST(Node_Shadowing, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    const auto parent = root.emplace<BasicTestObject>("");
    auto branch = root.branch("branch");
    const auto child = branch.emplace<BasicTestObject>("");

    REQUIRE(root.find<BasicTestObject>().get() == parent.get());
    REQUIRE(branch.find<BasicTestObject>().get() == child.get());
}

TEST(Node_ChildInstanceLookup, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<BasicTestObject>("root");
    auto branch = root.branch("branch");
    branch.emplace<BasicTestObject>("child");

    REQUIRE(branch.find<BasicTestObject>("child").exists());
    REQUIRE(branch.find<BasicTestObject>("root").exists());
    REQUIRE(!branch.find<BasicTestObject>("missing").exists());
}

TEST(Node_CollectSubtree, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<BasicTestObject>("root");
    auto branch = root.branch("branch");
    branch.emplace<BasicTestObject>("child");

    REQUIRE(root.collect<BasicTestObject>().size() == 2);
    REQUIRE(branch.collect<BasicTestObject>().size() == 1);
    REQUIRE(root.children<BasicTestObject>().size() == 1);
}

TEST(Node_Remove, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    const auto object = root.emplace<BasicTestObject>("");

    nodes.ops.destroyBranch(object.id());

    REQUIRE(!root.find<BasicTestObject>().exists());
}

TEST(Node_RemoveInstance, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    const auto first = root.emplace<BasicTestObject>("first");
    root.emplace<BasicTestObject>("second");

    nodes.ops.destroyBranch(first.id());

    REQUIRE(!root.find<BasicTestObject>("first").exists());
    REQUIRE(root.find<BasicTestObject>("second").exists());
    REQUIRE(root.collect<BasicTestObject>().size() == 1);
}

}
