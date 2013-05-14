//------------------------------------------------------------------------------
// <license file="NativeNumber.cs">
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
using System.Text;
using System.Globalization;

namespace EcmaScript.NET.Types
{

    /// <summary>
    /// This class implements the Number native object.
    /// 
    /// See ECMA 15.7.
    /// </summary>
    sealed class BuiltinNumber : IdScriptableObject
    {

        public const double NaN = double.NaN;

        public const double POSITIVE_INFINITY = double.PositiveInfinity;
        public const double NEGATIVE_INFINITY = double.NegativeInfinity;
        public const double MAX_VALUE = 1.7976931348623157e308;
        public const double MIN_VALUE = 5e-324;

        public static readonly double NegativeZero = BitConverter.Int64BitsToDouble (unchecked ((long)0x8000000000000000L));

        override public string ClassName
        {
            get
            {
                return "Number";
            }

        }


        private static readonly object NUMBER_TAG = new object ();

        private const int MAX_PRECISION = 100;

        internal static void Init (IScriptable scope, bool zealed)
        {
            BuiltinNumber obj = new BuiltinNumber (0.0);
            obj.ExportAsJSClass (MAX_PROTOTYPE_ID, scope, zealed
                , ScriptableObject.DONTENUM | ScriptableObject.READONLY | ScriptableObject.PERMANENT);
        }

        private BuiltinNumber (double number)
        {
            doubleValue = number;
        }

        protected internal override void FillConstructorProperties (IdFunctionObject ctor)
        {
            const int attr = ScriptableObject.DONTENUM | ScriptableObject.PERMANENT | ScriptableObject.READONLY;

            ctor.DefineProperty ("NaN", NaN, attr);
            ctor.DefineProperty ("POSITIVE_INFINITY", POSITIVE_INFINITY, attr);
            ctor.DefineProperty ("NEGATIVE_INFINITY", NEGATIVE_INFINITY, attr);
            ctor.DefineProperty ("MAX_VALUE", MAX_VALUE, attr);
            ctor.DefineProperty ("MIN_VALUE", MIN_VALUE, attr);

            base.FillConstructorProperties (ctor);
        }

        protected internal override void InitPrototypeId (int id)
        {
            string s;
            int arity;

            switch (id) {
                case Id_constructor:
                    arity = 1;
                    s = "constructor";
                    break;
                case Id_toString:
                    arity = 1;
                    s = "toString";
                    break;
                case Id_toLocaleString:
                    arity = 1;
                    s = "toLocaleString";
                    break;
                case Id_toSource:
                    arity = 0;
                    s = "toSource";
                    break;
                case Id_valueOf:
                    arity = 0;
                    s = "valueOf";
                    break;
                case Id_toFixed:
                    arity = 1;
                    s = "toFixed";
                    break;
                case Id_toExponential:
                    arity = 1;
                    s = "toExponential";
                    break;
                case Id_toPrecision:
                    arity = 1;
                    s = "toPrecision";
                    break;
                default:
                    throw new ArgumentException (Convert.ToString (id));

            }
            InitPrototypeMethod (NUMBER_TAG, id, s, arity);
        }

        public override object ExecIdCall (IdFunctionObject f, Context cx, IScriptable scope, IScriptable thisObj, object [] args)
        {
            if (!f.HasTag (NUMBER_TAG)) {
                return base.ExecIdCall (f, cx, scope, thisObj, args);
            }
            int id = f.MethodId;
            if (id == Id_constructor) {
                double val = (args.Length >= 1) ? ScriptConvert.ToNumber (args [0]) : 0.0;
                if (thisObj == null) {
                    // new Number(val) creates a new Number object.
                    return new BuiltinNumber (val);
                }
                // Number(val) converts val to a number value.
                return val;
            }

            // The rest of Number.prototype methods require thisObj to be Number
            BuiltinNumber nativeNumber = (thisObj as BuiltinNumber);
            if (nativeNumber == null)
                throw IncompatibleCallError (f);
            double value = nativeNumber.doubleValue;

            switch (id) {


                case Id_toLocaleString:
                case Id_toString:
                    return ImplToString (value, args);

                case Id_toSource:
                    return "(new Number(" + ScriptConvert.ToString (value) + "))";


                case Id_valueOf:
                    return value;


                case Id_toFixed:
                    return ImplToFixed (value, args);


                case Id_toExponential:
                    return ImplToExponential (value, args);


                case Id_toPrecision:
                    return ImplToPrecision (value, args);

                default:
                    throw new ArgumentException (Convert.ToString (id));

            }
        }

        internal static string ImplToString (double value, object [] args)
        {
            int radix = (args == null || args.Length == 0) ? 10 : ScriptConvert.ToInt32 (args [0]);
            return ImplToString (value, radix);
        }

        internal static string ImplToString (double d, int radix)
        {
            string ret = HandleSpecialDoubles (d);
            if (ret != null)
                return ret;

            AssertValidRadix (radix);

            // Format 'g' is pretty close what we want but it has the following drawbacks
            //  - if exponent is greater than 15 .NET switches to exponential formatting
            //    while ecma wants normal formatting
            //  - if exponent is smaller than -5 .NET switches to exponential formatting
            //    while ecma wants normal formatting
            //  - 'g' pads the exponent with a zero if length is lesser than 10. So we
            //    remove it.            
            int exponent = GetExponent (d);

            if ((exponent >= 15 && exponent < 21)) {
                string tmp = d.ToString ("e" + (exponent + 1));
                tmp = tmp.Replace (".", "");
                tmp = tmp.Substring (0, tmp.Length - 6);
                return tmp;
            }
            else if (exponent <= -5 && exponent > -7) {
                string tmp = d.ToString ("f21");
                tmp = tmp.TrimEnd ('0');
                tmp = tmp.TrimEnd ('.');
                return tmp;
            }
            else if (exponent <= -7 && exponent > -10) {
                string tmp = d.ToString ("g");
                tmp = tmp.Substring (0, tmp.Length - 2) + tmp [tmp.Length - 1];
                return tmp;
            }

            return d.ToString ("g");
        }

        static object ImplToFixed (double value, object [] args)
        {
            if (args.Length < 1)
                return ImplToString (value, args);

            string ret = HandleSpecialDoubles (value);
            if (ret == null) {
                // Fixed-Point Format : Used for strings in the following form: 
                // "[-]m.dd...d" 
                ret = value.ToString ("f" + GetPrecision (args [0]));
            }
            return ret;
        }

        internal static int GetPrecision (object arg)
        {
            int precision = ScriptConvert.ToInt32 (arg);
            AssertValidPrecision (precision);
            return precision;
        }
    
        internal static int GetNoOfDecimals (double value)
        {
            string str = value.ToString ("e23", CultureInfo.InvariantCulture);
            int idxOfSep = str.IndexOfAny (new char[] { '.', 'e' });
            if (idxOfSep == -1)
                idxOfSep = str.Length;
            return idxOfSep;                
        }
        
        internal static int GetExponent (double value)
        {
            if (value == 0.0)
                return 0;

            value = Math.Abs (value);
            int exponent = 0;
            if (value >= 1.0) {
                while (value >= 10.0) {
                    exponent++;
                    value /= 10.0;
                }
            }
            else {
                while (value <= 1.0) {
                    exponent--;
                    value *= 10.0;
                }
            }
            return exponent;
        }


        internal static int AssertValidPrecision (int precision)
        {
            if (precision < 0 || precision > MAX_PRECISION) {
                string msg = ScriptRuntime.GetMessage ("msg.bad.precision", ScriptConvert.ToString (precision));
                throw ScriptRuntime.ConstructError ("RangeError", msg);
            }
            return precision;
        }

        internal static int AssertValidRadix (int radix)
        {
            if ((radix < 2) || (radix > 36)) {
                throw Context.ReportRuntimeErrorById ("msg.bad.radix", Convert.ToString (radix));
            }
            if (radix != 10)
                throw new NotImplementedException ("Radix beside 10 is not implemented.");
            return radix;
        }

        internal static string HandleSpecialDoubles (double d)
        {
            if (double.IsNaN (d))
                return "NaN";
            if (d == System.Double.PositiveInfinity)
                return "Infinity";
            if (d == System.Double.NegativeInfinity)
                return "-Infinity";
            if (d == 0.0)
                return "0";
            return null;
        }

        static string ImplToExponential (double value, object [] args)
        {
            int prec = 16;
            if (args.Length > 0)
                prec = GetPrecision (args [0]);
            return ImplToExponential (value, prec);
        }

        static string ImplToExponential (double value, int precision)
        {
            string ret = HandleSpecialDoubles (value);
            if (ret == null) {
                ret = value.ToString (GetFormatString (precision) + "e+0", CultureInfo.InvariantCulture);
            }
            return ret;
        }

        static string GetFormatString (int precision)
        {
            // TODO: Cache those format strings?
            string formatString = "#.";
            for (int i = 0; i < precision; i++)
                formatString += "0";
            return formatString;
        }

        private object ImplToPrecision (double value, object [] args)
        {
            string ret = HandleSpecialDoubles (value);
            if (ret == null) {
                if (args.Length == 0)
                    return ImplToString (value, args);
                int prec = GetPrecision (args [0]);
                AssertValidPrecision (prec);
                int noOfDecimals = GetNoOfDecimals (value);                        
                prec = prec - noOfDecimals;        
                if (prec < 1)
                    prec = 1;                    
                ret = value.ToString ("f" + prec, CultureInfo.InvariantCulture);
            }            
            return ret;
        }

        public override string ToString ()
        {
            return ImplToString (doubleValue, 10);
        }

        protected internal override int FindPrototypeId (string s)
        {
            int id;
            #region Generated PrototypeId Switch
        L0: {
                id = 0;
                string X = null;
                int c;
            L:
                switch (s.Length) {
                    case 7:
                        c = s [0];
                        if (c == 't') { X = "toFixed"; id = Id_toFixed; }
                        else if (c == 'v') { X = "valueOf"; id = Id_valueOf; }
                        break;
                    case 8:
                        c = s [3];
                        if (c == 'o') { X = "toSource"; id = Id_toSource; }
                        else if (c == 't') { X = "toString"; id = Id_toString; }
                        break;
                    case 11:
                        c = s [0];
                        if (c == 'c') { X = "constructor"; id = Id_constructor; }
                        else if (c == 't') { X = "toPrecision"; id = Id_toPrecision; }
                        break;
                    case 13:
                        X = "toExponential";
                        id = Id_toExponential;
                        break;
                    case 14:
                        X = "toLocaleString";
                        id = Id_toLocaleString;
                        break;
                }
                if (X != null && X != s && !X.Equals (s))
                    id = 0;
            }
        EL0:

            #endregion
            return id;
        }

        #region PrototypeIds
        private const int Id_constructor = 1;
        private const int Id_toString = 2;
        private const int Id_toLocaleString = 3;
        private const int Id_toSource = 4;
        private const int Id_valueOf = 5;
        private const int Id_toFixed = 6;
        private const int Id_toExponential = 7;
        private const int Id_toPrecision = 8;
        private const int MAX_PROTOTYPE_ID = 8;
        #endregion

        private double doubleValue;


    }
}
