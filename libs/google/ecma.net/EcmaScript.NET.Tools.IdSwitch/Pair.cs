//------------------------------------------------------------------------------
// <license file="Pair.cs">
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

using EcmaScript.NET;

namespace EcmaScript.NET.Tools.IdSwitch {

    public class Pair {

        public string Value;
        public string Id;

        public Pair(string id, string value) {
            this.Id = id;
            this.Value = value;			
        }

    }

}