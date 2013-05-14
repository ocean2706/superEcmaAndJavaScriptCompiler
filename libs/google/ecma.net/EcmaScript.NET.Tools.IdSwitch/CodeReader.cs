//------------------------------------------------------------------------------
// <license file="CodeReader.cs">
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
using System.Text.RegularExpressions;

namespace EcmaScript.NET.Tools.IdSwitch {
    class CodeReader {

		private static Regex RegExpRegion = new Regex(@"\s+#region (InstanceIds|PrototypeIds|Ids)([-\+\.A-z0-9\s_=;]+)#endregion");
		private static Regex RegExpPair	= new Regex(@"\bprivate\b\s+\bconst\b\s+\bint\b\s+(Id_[A-z0-9_]+)\s+=\s([0-9]+|[A-z\.])");		

				
        public ArrayList Read(string fileName) {
            ArrayList pairGroups = new ArrayList();

			string content = string.Empty;
			using (StreamReader sr = new StreamReader(fileName)) {
				content = sr.ReadToEnd();
			}
			
			foreach (Match match in RegExpRegion.Matches(content)) {				
				PairGroup pg = new PairGroup();
				pg.Type = match.Groups[1].Value;
			
				foreach (Match mPair in RegExpPair.Matches(match.Groups[2].Value)) {
					string id = mPair.Groups[1].Value;
                    Pair pair = new Pair (id, id.Substring (3));
                    switch (pair.Value) {
                        case "STAR": pair.Value = "$*"; break;
                        case "UNDERSCORE": pair.Value = "$_"; break;
                        case "AMPERSAND": pair.Value = "$&"; break;
                        case "PLUS": pair.Value = "$+"; break;
                        case "BACKQUOTE": pair.Value = "$`"; break;
                        case "QUOTE": pair.Value = "$'"; break;
                        case "DOLLAR_0": pair.Value = "$0"; break;
                        case "DOLLAR_1": pair.Value = "$1"; break;
                        case "DOLLAR_2": pair.Value = "$2"; break;
                        case "DOLLAR_3": pair.Value = "$3"; break;
                        case "DOLLAR_4": pair.Value = "$4"; break;
                        case "DOLLAR_5": pair.Value = "$5"; break;
                        case "DOLLAR_6": pair.Value = "$6"; break;
                        case "DOLLAR_7": pair.Value = "$7"; break;
                        case "DOLLAR_8": pair.Value = "$8"; break;
                        case "DOLLAR_9": pair.Value = "$9"; break;
                    }
                        
					pg.Pairs.Add(pair);					
				}


				pairGroups.Add(pg);

				if (pg.Pairs.Count == 0) {
					Console.WriteLine("WARN: Found PairGroup ("  + pg.Type + ") but no pairs.");
				}
			}

			return pairGroups;
        }
    }
}
