#include "Editor.h"

int main(int argc, char** argv)
{
    Editor::SparkEditor editor;
    editor.Init();

    editor.Start();

    editor.Close();

    return 0;
}