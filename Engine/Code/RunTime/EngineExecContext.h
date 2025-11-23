#pragma once

enum ExecMode
{
    Editor,
    Simulation,
    GamePlay
};



struct ExecContext
{
    ExecMode execMode;
};