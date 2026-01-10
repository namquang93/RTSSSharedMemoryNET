using System;
using System.Linq;
using RTSSSharedMemoryNET;

namespace RTSSDemo
{
    class Program
    {
        static void Main(string[] args)
        {
            //enforces a nice cleanup
            //just hitting X or Ctrl+C normally won't actually dispose the using() below
            ExitHandler.Init(ctrlType => {
                Console.WriteLine("\nCleaning up and exiting...");
                return true; //cancel event
            });

            ///////////////////////////////////////////////////////////////////

            Console.WriteLine("RTSSSharedMemoryNET - Frametime Graph Demo");
            Console.WriteLine("===========================================\n");
            Console.WriteLine($"RTSS Version: {OSD.Version}\n");

            Console.WriteLine("Current OSD entries:");
            var osdEntries = OSD.GetOSDEntries();
            foreach( var osd in osdEntries )
            {
                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine(osd.Owner);
                Console.ResetColor();
                Console.WriteLine("{0}\n", osd.Text);
            }

            ///////////////////////////////////////////////////////////////////

            Console.WriteLine("Current app entries with GPU contexts:");
            var appEntries = OSD.GetAppEntries().Where( x => (x.Flags & AppFlags.MASK) != AppFlags.None ).ToArray();
            foreach( var app in appEntries )
            {
                Console.ForegroundColor = ConsoleColor.Magenta;
                Console.WriteLine("{0}:{1}", app.ProcessId, app.Name);
                Console.ResetColor();
                Console.WriteLine("{0}, {1}FPS", app.Flags, app.InstantaneousFrames);
            }
            Console.WriteLine();

            ///////////////////////////////////////////////////////////////////

            Console.WriteLine("Graph Embedding Demo");
            Console.WriteLine("====================\n");
            Console.WriteLine("This demo shows how to embed frametime/framerate graphs.");
            Console.WriteLine("Graph tags like <G=<FT>> are automatically converted to embedded graph objects.\n");
            Console.WriteLine("The graphs will appear in any running 3D application's OSD.\n");

            using( var osd = new OSD("RTSSDemo") )
            {
                Console.WriteLine("Enter OSD text (use <G=<FT>> for frametime graph, <G=<FR>> for framerate graph):");
                Console.WriteLine("Example: Frametime: <G=<FT>>");
                Console.WriteLine("Example: FPS: <G=<FR>>");
                Console.WriteLine("Example: Frametime: <G=<FT>>\\nFPS: <G=<FR>>");
                Console.WriteLine("\nPress Ctrl+C to exit.\n");

                while( true )
                {
                    Console.Write("> ");
                    var text = Console.ReadLine();

                    //if we hit Ctrl+C while waiting for ReadLine, it returns null
                    if( text == null )
                        break;

                    try
                    {
                        osd.Update(text);
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("✓ OSD updated successfully!");
                        Console.ResetColor();
                    }
                    catch (Exception ex)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine($"✗ Error: {ex.Message}");
                        Console.ResetColor();
                    }
                }
            }
        }
    }
}
