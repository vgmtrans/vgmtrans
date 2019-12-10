#define CATCH_CONFIG_RUNNER
#include <catch.hpp>

/* Get rid of this eventually */
#include "Root.h"

class TestRoot : public VGMRoot {
    void UI_SetRootPtr(VGMRoot **theRoot) override;
    void UI_Exit() override {}
    std::wstring UI_GetOpenFilePath(const std::wstring &suggestedFilename = L"",
                                    const std::wstring &extension = L"") override {
        return {};
    }
    virtual std::wstring UI_GetSaveFilePath(const std::wstring &suggestedFilename,
                                            const std::wstring &extension = L"") override {
        return {};
    }
    virtual std::wstring UI_GetSaveDirPath(const std::wstring &suggestedDir = L"") override {
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
