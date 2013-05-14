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
using System.Collections;

namespace EcmaScript.NET.Tools.IdSwitch {

	class Program {

		public static void Main(string[] args) {
            CheckFile (@"C:\Development\EcmaScript.NET 1.0\EcmaScript.NET\Types\RegExp\BuiltinRegExpCtor.cs");
			Console.ReadLine();
		}

		private static void CheckDir(string dir) {
			foreach (string subDir in Directory.GetDirectories(dir)) {
				CheckDir(subDir);
			}
			foreach (string file in Directory.GetFiles(dir, "*.cs")) {
				CheckFile(file);
			}
		}

		private static int CheckFile(string fileName) {
			CodeReader r = new CodeReader();
			ArrayList pairGroups = r.Read(fileName);
			if (pairGroups.Count == 0) {				
				return -1;
			}			
			
			CodeWriter w = new CodeWriter();
			if (!w.Write(fileName, pairGroups)) {				
				return -2;
			}			
			
			return 0;
		}

	}
}
