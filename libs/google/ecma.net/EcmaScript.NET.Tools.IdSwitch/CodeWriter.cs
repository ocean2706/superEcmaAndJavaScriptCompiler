//------------------------------------------------------------------------------
// <license file="CodeWriter.cs">
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
	class CodeWriter {


		public bool Write(string fileName, ArrayList pairGroups) {
			if (pairGroups.Count == 0)
				return false;

			Console.Write(Path.GetFileName(fileName));
			Console.Write("\t" + pairGroups.Count);

			string tmpFileName = Path.ChangeExtension(fileName, "bak." + DateTime.Now.Ticks.ToString());
			
			using (StreamWriter sw = new StreamWriter(tmpFileName)) {			
				string generated = string.Empty;
				using (StreamReader sr = new StreamReader(fileName)) {
					do {
						string line = sr.ReadLine();
						if (line == null)
							break;

						if (generated != string.Empty) {
							if (line.IndexOf("#endregion") != -1) {
								sw.WriteLine(generated);
								sw.WriteLine(line);
								generated = string.Empty;								
							}
							continue;
						}

						int idx;
						
						idx = line.IndexOf("#region Generated PrototypeId Switch");
						if (idx != -1) {
							PairGroup pg = GetPairGroup(pairGroups, "PrototypeIds");
							generated = Generate(pg);	
							sw.WriteLine(line);
							pairGroups.Remove(pg);
							continue;
						} 

						idx = line.IndexOf("#region Generated InstanceId Switch");
						if (idx != -1) {
							PairGroup pg = GetPairGroup(pairGroups, "InstanceIds");
							generated = Generate(pg);	
							sw.WriteLine(line);
							pairGroups.Remove(pg);
							continue;
						} 

						idx = line.IndexOf("#region Generated Id Switch");
						if (idx != -1) {
							PairGroup pg = GetPairGroup(pairGroups, "Ids");
							generated = Generate(pg);	
							sw.WriteLine(line);
							pairGroups.Remove(pg);
							continue;
						} 

						sw.WriteLine(line);

					} while (true);
				}
			}
						
			File.Copy(tmpFileName, fileName, true);			
			File.Delete(tmpFileName);			
			
			Console.Write("\tUPDATED");			
			Console.Write("\t" + pairGroups.Count);			
			Console.WriteLine();

			return false;
		}

		public PairGroup GetPairGroup(ArrayList pairGroups, string type) {
			foreach (PairGroup pg in pairGroups) {
				if (pg.Type == type)
					return pg;
			}
			return null;
		}

		public string Generate(PairGroup pg) {
			Generator g = new Generator();
			g.generateSwitch(
				(Pair[])pg.Pairs.ToArray(
				typeof(Pair)), "0");
			return g.P.ToString();	
		}

	}

}
