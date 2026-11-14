// Tests for feet on the ground's sums (src/PlayerbotsPlusGroundGrid.cpp),
// which have no game types in them. Not part of the server build:
// AzerothCore only compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusGroundGrid.h"

#include <cstdio>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Check(char const* what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what);
        }
    }

    void Eq(char const* what, long long got, long long want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got %lld, want %lld\n", what, got, want);
        }
    }
}

int main()
{
    using PbpGround::Cell;

    // ---- the squares, rounded as playerbots rounds them ------------------------------------
    {
        Cell const c = PbpGround::CellOf(-9680.0f, -2850.0f, 52.0f);   // Dire Condors in Redridge
        Eq("x: -193.6 to -194", c.x, -194);
        Eq("y: -57 exactly", c.y, -57);
        Eq("z: 1.04 to 1", c.z, 1);
        Eq("halves away from zero, up", PbpGround::CellOf(125.0f, 0.0f, 0.0f).x, 3);
        Eq("halves away from zero, down", PbpGround::CellOf(0.0f, 0.0f, -25.0f).z, -1);
        Eq("just under a half", PbpGround::CellOf(124.9f, 0.0f, 0.0f).x, 2);
    }

    // ---- a point made out of a square ---------------------------------------------------------
    {
        Cell c;
        Check("a square's middle is on the grid", PbpGround::OnGrid(-9700.0f, -2850.0f, 50.0f, c));
        Check("...and is that square", c == Cell{ -194, -57, 1 });
        Check("a real height is not", !PbpGround::OnGrid(-9700.0f, -2850.0f, 52.3f, c));
        Check("nor a real x", !PbpGround::OnGrid(-9700.5f, -2850.0f, 50.0f, c));
        Check("nor a real y", !PbpGround::OnGrid(-9700.0f, -2849.0f, 50.0f, c));
        Check("Valdolar's spot in Teldrassil", PbpGround::OnGrid(9750.0f, 1100.0f, 1300.0f, c) && c.z == 26);
        Check("height 0 is on it too", PbpGround::OnGrid(-4500.0f, -12300.0f, 0.0f, c) && c.z == 0);
    }

    // ---- squares as one number ------------------------------------------------------------------
    {
        Check("the same square, the same key",
              PbpGround::Key(0, { -194, -57, 1 }) == PbpGround::Key(0, { -194, -57, 1 }));
        Check("a neighbour, another", PbpGround::Key(0, { -194, -57, 1 }) != PbpGround::Key(0, { -193, -57, 1 }));
        Check("one up, another", PbpGround::Key(0, { -194, -57, 1 }) != PbpGround::Key(0, { -194, -57, 2 }));
        Check("either side of zero apart", PbpGround::Key(0, { -1, 0, 0 }) != PbpGround::Key(0, { 1, 0, 0 }));
        Check("another map, another", PbpGround::Key(0, { 5, 5, 5 }) != PbpGround::Key(1, { 5, 5, 5 }));
        Check("y and z kept apart", PbpGround::Key(530, { 0, 1, 0 }) != PbpGround::Key(530, { 0, 0, 1 }));
    }

    // ---- nearest the middle -----------------------------------------------------------------------
    {
        Cell const c{ -194, -57, 1 };
        Eq("the middle itself", static_cast<long long>(PbpGround::FromMiddle2(c, -9700.0f, -2850.0f, 50.0f)), 0);
        Eq("10 across and 10 up",
           static_cast<long long>(PbpGround::FromMiddle2(c, -9690.0f, -2850.0f, 60.0f) + 0.5f), 200);
        Check("a spawn on the ground below beats one at the edge",
              PbpGround::FromMiddle2(c, -9700.0f, -2850.0f, 28.0f) <
                  PbpGround::FromMiddle2(c, -9675.0f, -2825.0f, 50.0f));
    }

    // ---- falls ------------------------------------------------------------------------------------
    Eq("no height, no fall", PbpGround::FallMs(0.0f), 0);
    Eq("nor a negative one", PbpGround::FallMs(-3.0f), 0);
    Eq("half the game's gravity: a second", PbpGround::FallMs(19.29110527f / 2.0f), 1000);
    Eq("25 yards", PbpGround::FallMs(25.0f), 1610);

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
