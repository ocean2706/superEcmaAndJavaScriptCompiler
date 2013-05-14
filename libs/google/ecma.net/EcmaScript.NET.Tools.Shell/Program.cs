//------------------------------------------------------------------------------
// <license file="Program.cs">
//     
//      The use and distribution terms for this software are contained in the file
//      named 'LICENSE', which can be found in the resources directory of this
//		distribution.
//
//      By using this software in any fashion, you are agreeing to be bound by the
//      terms of this license.
//     
// </license>                                                                
//------------------------------------------------------------------------------

using System;
using System.IO;
using System.Reflection;
using System.Collections.Specialized;
using System.Diagnostics;

using EcmaScript.NET;
using EcmaScript.NET.Types;
using EcmaScript.NET.Types.Cli;
using EcmaScript.NET.Types.E4X;
using EcmaScript.NET.Attributes;

namespace EcmaScript.NET.Tools.Shell
{

    using System;
    using System.Globalization;

    /// <summary>
    /// The shell program.
    /// 
    /// Can execute scripts interactively or in batch mode at the command line.
    /// </summary>    
    [EcmaScriptClass ("Shell")]
    public class Program : BuiltinGlobalObject
    {

        public override string ClassName
        {
            get
            {
                return "global";
            }
        }

        private Program ()
        {
            ;
        }

        private static StringCollection input = new StringCollection ();
        private static bool pauseAfterExecution = false;
        private static int repeatCount = 1;


        /// <summary>
        /// Main entry point.
        /// 
        /// Process arguments as would a normal program. Also
        /// create a new Context and associate it with the current thread.
        /// Then set up the execution environment and begin to
        /// execute scripts.
        /// </summary>
        [STAThread]
        public static int Main (string [] args)
        {
            int ret = 0;
                        
            if (!ParseArgs (args))
                return -1;

            for (int i = 0; i < repeatCount; i++) {
                try {

                    Program shell = new Program ();
                    do {
                        shell = new Program ();

                        // Associate a new Context with this thread
                        using (Context cx = Context.Enter ()) {
                            
                            // Initialize the standard objects (Object, Function, etc.)
                            // This must be done before scripts can be executed.                    
                            cx.InitStandardObjects (shell);
                            
                            // Set en-US as culture
                            cx.CurrentCulture = new CultureInfo ("en-US");

                            // Init cli support
                            CliPackage.Init (shell);


                            if (input.Count == 0) {
                                shell.ProcessSource (cx, null);
                            }
                            else {
                                foreach (string file in input) {
                                    shell.ProcessSource (cx, file);
                                }
                                shell.quitting = true;
                            }

                        }

                    } while (!shell.quitting);

                }
                catch (Exception ex) {
                    Console.Error.WriteLine (ex.GetType ().FullName + ": " + ex.Message + ": " + ex.ToString ());
                    ret = -1;
                }
            }

            if (pauseAfterExecution || Debugger.IsAttached) {
                Console.WriteLine ("Press 'Any' key to continue.");
                Console.ReadLine ();
            }

            return ret;
        }

        private static bool ParseArgs (string [] args)
        {
            for (int i = 0; i < args.Length; i++) {
                string arg = args [i];
                if (arg [0] == '-') {
                    if (arg.Length < 2)
                        return false;

                    switch (arg [1]) {
                        case 'p':
                            pauseAfterExecution = true;
                            break;

                        case 'r':
                            repeatCount = int.Parse (args [i + 1]);
                            i++;
                            break;

                    }
                }
                else {
                    input.Add (args [i]);
                }
            }

            return true;
        }


        public static void ShowHelp ()
        {
            Console.Error.WriteLine ("Usage: EcmaScript.NET.Tools.Shell.exe [options] <file> [file ...]");
            Console.Error.WriteLine ("Available options:");
            Console.Error.WriteLine ("\t-p        Pause after script execution");
            Console.Error.WriteLine ("\t-r [0-9]  Repeate scripts for n times");
        }

        [EcmaScriptFunction ("reset")]
        public void Reset ()
        {
            resetting = true;
        }

        /// <summary>
        /// Quit the shell.
        /// This only affects the interactive mode.
        /// </summary>
        [EcmaScriptFunction ("quit")]
        public void Quit ()
        {
            quitting = true;
        }

        /// <summary>
        /// Load and execute a set of JavaScript source files.        
        /// </summary>
        [EcmaScriptFunction ("load")]
        public void Load (params object [] sources)
        {
            foreach (string src in sources) {
                ProcessSource (Context.CurrentContext, src);
            }
        }


        /// <summary> Evaluate JavaScript source.
        /// 
        /// </summary>
        /// <param name="cx">the current context
        /// </param>
        /// <param name="filename">the name of the file to compile, or null
        /// for interactive mode.
        /// </param>
        private void ProcessSource (Context cx, string filename)
        {

            if (filename == null) {
                Console.WriteLine ("EcmaScript.NET v" + typeof (Context).Assembly
                    .GetName ().Version);

                ScriptOrFnNode script = null;
                string sourceName = "<stdin>";
                int lineno = 1;
                bool hitEOF = false;
                do {
                    int startline = lineno;
                    Console.Error.Write ("js> ");
                    Console.Error.Flush ();

                    string source = "";
                    // Collect lines of source to compile.
                    while (true) {
                        string newline = Console.In.ReadLine ();
                        if (newline == null) {
                            hitEOF = true;
                            break;
                        }
                        source = source + newline + "\n";
                        lineno++;
                        // Continue collecting as long as more lines
                        // are needed to complete the current
                        // statement.  stringIsCompilableUnit is also
                        // true if the source statement will result in
                        // any error other than one that might be
                        // resolved by appending more source.
                        if ((script = cx.IsCompilableUnit (source)) != null)
                            break;
                    }


                    try {
                        object result = cx.EvaluateString (this, source, sourceName, startline, (Object)null);
                        if (result != Context.UndefinedValue) {
                            Console.Error.WriteLine (ScriptConvert.ToString (result));
                        }
                    }
                    catch (Exception ex) {
                        PrintException (ex);
                    }                   

                    if (quitting || resetting) {
                        // The user executed the quit() function.
                        break;
                    }
                }
                while (!hitEOF);

                Console.Error.WriteLine ();
            }
            else {
                // Here we evalute the entire contents of the file as
                // a script. Text is printed only if the print() function
                // is called.
                using (StreamReader sr = new StreamReader (filename)) {
                    try {
                        cx.EvaluateReader (this, sr, filename, 1, null);
                    } catch (Exception ex) {
                        PrintException (ex);
                    }                   
                }
            }
            GC.Collect ();
        }

        private bool quitting;
        private bool resetting;

        void PrintException (Exception ex) {
            if (ex is EcmaScriptRuntimeException) {
                EcmaScriptRuntimeException e = (EcmaScriptRuntimeException)ex;
                Console.Error.WriteLine ("js: " + e.Message);
                if (e.ScriptStackTrace != string.Empty)
                    Console.Error.WriteLine (e.ScriptStackTrace);
            }
            else if (ex is EcmaScriptThrow) {
                EcmaScriptThrow e = (EcmaScriptThrow)ex;
                Console.Error.WriteLine ("js: \"{0}\", line {1}: exception from uncaught throw: {2}",
                    e.SourceName, e.LineNumber, e.Value);
            }
            else if (ex is EcmaScriptError) {
                EcmaScriptError e = (EcmaScriptError)ex;

                Console.Error.WriteLine ("js: " + e.Message);
                Console.Error.WriteLine (e.ScriptStackTrace);
            }            
            else {
                Console.Error.WriteLine (ex.ToString ());
            }
        }
    }
}

