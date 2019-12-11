#define CATCH_CONFIG_RUNNER
#include <catch.hpp>

/* Get rid of this eventually */
#include "Root.h"

class TestRoot : public VGMRoot {
    void UI_SetRootPtr(VGMRoot **theRoot) override;
    void UI_Exit() override {}
    std::string UI_GetOpenFilePath(const std::string &suggestedFilename = "",
                                   const std::string &extension = "") override {
        return {};
    }
    virtual std::string UI_GetSaveFilePath(const std::string &suggestedFilename,
                                           const std::string &extension = "") override {
        return {};
    }
    virtual std::string UI_GetSaveDirPath(const std::string &suggestedDir = "") override {
        return {};
    }
};

TestRoot root;

void TestRoot::UI_SetRootPtr(VGMRoot **theRoot) {
    *theRoot = &root;
}

int main(int argc, char *argv[]) {
    root.Init();
    int result = Catch::Session().run(argc, argv);

    return result;
}
