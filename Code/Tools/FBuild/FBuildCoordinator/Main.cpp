// Main
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildCoordinatorOptions.h"
#include "Coordinator/Coordinator.h"
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/Helpers/CtrlCHandler.h"

#include "Core/Profile/Profile.h"
#include "Core/Process/SystemMutex.h"
#include "Core/Tracing/Tracing.h"

// system
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h"
#endif

// Global Data
//------------------------------------------------------------------------------
// only allow 1 worker per system
SystemMutex g_OneProcessMutex( "Global\\FBuildCoordinator" );

// Return Codes
//------------------------------------------------------------------------------
enum ReturnCodes
{
    FBUILD_OK                               = 0,
    FBUILD_BAD_ARGS                         = -1,
    FBUILD_ALREADY_RUNNING                  = -2
};

// Headers
//------------------------------------------------------------------------------
int Main( const AString & args );

// main
//------------------------------------------------------------------------------
#if defined( __WINDOWS__ )
    PRAGMA_DISABLE_PUSH_MSVC( 28251 ) // don't complain about missing annotations on WinMain
    int WINAPI WinMain( HINSTANCE /*hInstance*/,
                        HINSTANCE /*hPrevInstance*/,
                        LPSTR lpCmdLine,
                        int /*nCmdShow*/ )
    {
        AStackString<> args( lpCmdLine );
        const int32_t result = Main( args );
        PROFILE_SYNCHRONIZE
        return result;
    }
    PRAGMA_DISABLE_POP_MSVC
#else
    int main(int argc, char * argv[])
    {
        AStackString<> args;
        for ( int i=1; i<argc; ++i ) // NOTE: Skip argv[0] exe name
        {
            if ( i > 0 )
            {
                args += ' ';
            }
            args += argv[ i ];
        }

        // This wrapper is purely for profiling scope
        int result = Main( args );
        PROFILE_SYNCHRONIZE // make sure no tags are active and do one final sync
        return result;
    }
#endif

#include <stdio.h>

// Main
//------------------------------------------------------------------------------
int Main( const AString & args )
{
    // handle cmd line args
    FBuildCoordinatorOptions options;
    if ( options.ProcessCommandLine( args ) == false )
    {
        return FBUILD_BAD_ARGS;
    }

    // only allow 1 worker per system
    Timer t;
    while ( g_OneProcessMutex.TryLock() == false )
    {
        // retry for upto 2 seconds, to allow some time for old worker to close
        if ( t.GetElapsed() > 5.0f )
        {
            OUTPUT( "An FBuildCoordinator is already running!\n" );
            return FBUILD_ALREADY_RUNNING;
        }
        Thread::Sleep(100);
    }

    Coordinator coordinator( args );

    return static_cast<int>(coordinator.Start());
}
