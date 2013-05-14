//------------------------------------------------------------------------------
// <license file="CodePrinter.cs">
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

namespace EcmaScript.NET.Tools.IdSwitch {
	
	public class CodePrinter
	{
		virtual public string LineTerminator
		{
			get
			{
				return lineTerminator;
			}
			
			set
			{
				lineTerminator = value;
			}
			
		}
		virtual public int IndentStep
		{
			get
			{
				return indentStep;
			}
			
			set
			{
				indentStep = value;
			}
			
		}
		virtual public int IndentTabSize
		{
			get
			{
				return indentTabSize;
			}
			
			set
			{
				indentTabSize = value;
			}
			
		}
		virtual public int Offset
		{
			get
			{
				return offset;
			}
			
		}
		virtual public int LastChar
		{
			get
			{
				return offset == 0?- 1:buffer[offset - 1];
			}
			
		}
		
		// length of u-type escape like \u12AB
		private const int LITERAL_CHAR_MAX_SIZE = 6;
		
		private string lineTerminator = "\n";
		
		private int indentStep = 4;
		private int indentTabSize = 8;
		
		private char[] buffer = new char[1 << 12]; // 4K
		private int offset;
		
		public virtual void  clear()
		{
			offset = 0;
		}
		
		private int ensure_area(int area_size)
		{
			int begin = offset;
			int end = begin + area_size;
			if (end > buffer.Length)
			{
				int new_capacity = buffer.Length * 2;
				if (end > new_capacity)
				{
					new_capacity = end;
				}
				char[] tmp = new char[new_capacity];
				Array.Copy(buffer, 0, tmp, 0, begin);
				buffer = tmp;
			}
			return begin;
		}
		
		private int add_area(int area_size)
		{
			int pos = ensure_area(area_size);
			offset = pos + area_size;
			return pos;
		}
		
		public virtual void  p(char c)
		{
			int pos = add_area(1);
			buffer[pos] = c;
		}
		
		public virtual void  p(string s)
		{
			int l = s.Length;
			int pos = add_area(l);
			GetCharsFromString(s, 0, l, buffer, pos);
		}


		public static void GetCharsFromString(string sourceString, int sourceStart, int sourceEnd, char[] destinationArray, int destinationStart) {
			int sourceCounter;
			int destinationCounter;
			sourceCounter = sourceStart;
			destinationCounter = destinationStart;
			while (sourceCounter < sourceEnd) {
				destinationArray[destinationCounter] = (char)sourceString[sourceCounter];
				sourceCounter++;
				destinationCounter++;
			}
		}

		public void  p(char[] array)
		{
			p(array, 0, array.Length);
		}
		
		public virtual void  p(char[] array, int begin, int end)
		{
			int l = end - begin;
			int pos = add_area(l);
			Array.Copy(array, begin, buffer, pos, l);
		}
		
		public virtual void  p(int i)
		{
			p(System.Convert.ToString(i));
		}
		
		public virtual void  qchar(int c)
		{
			int pos = ensure_area(2 + LITERAL_CHAR_MAX_SIZE);
			buffer[pos] = '\'';
			pos = put_string_literal_char(pos + 1, c, false);
			buffer[pos] = '\'';
			offset = pos + 1;
		}
		
		public virtual void  qstring(string s)
		{
			int l = s.Length;
			int pos = ensure_area(2 + LITERAL_CHAR_MAX_SIZE * l);
			buffer[pos] = '"';
			++pos;
			for (int i = 0; i != l; ++i)
			{
				pos = put_string_literal_char(pos, s[i], true);
			}
			buffer[pos] = '"';
			offset = pos + 1;
		}
		
		private int put_string_literal_char(int pos, int c, bool in_string)
		{
			bool backslash_symbol = true;
			switch (c)
			{
				
				case '\b':  c = 'b'; break;
				
				case '\t':  c = 't'; break;
				
				case '\n':  c = 'n'; break;
				
				case '\f':  c = 'f'; break;
				
				case '\r':  c = 'r'; break;
				
				case '\'':  backslash_symbol = !in_string; break;
				
				case '"':  backslash_symbol = in_string; break;
				
				default:  backslash_symbol = false;
					break;
				
			}
			
			if (backslash_symbol)
			{
				buffer[pos] = '\\';
				buffer[pos + 1] = (char) c;
				pos += 2;
			}
			else if (' ' <= c && c <= 126)
			{
				buffer[pos] = (char) c;
				++pos;
			}
			else
			{
				buffer[pos] = '\\';
				buffer[pos + 1] = 'u';
				buffer[pos + 2] = digit_to_hex_letter(0xF & (c >> 12));
				buffer[pos + 3] = digit_to_hex_letter(0xF & (c >> 8));
				buffer[pos + 4] = digit_to_hex_letter(0xF & (c >> 4));
				buffer[pos + 5] = digit_to_hex_letter(0xF & c);
				pos += 6;
			}
			return pos;
		}
		
		private static char digit_to_hex_letter(int d)
		{
			return (char) ((d < 10)?'0' + d:'A' - 10 + d);
		}
		
		public virtual void  indent(int level)
		{
			int visible_size = indentStep * level;
			int indent_size, tab_count;
			if (indentTabSize <= 0)
			{
				tab_count = 0; indent_size = visible_size;
			}
			else
			{
				tab_count = visible_size / indentTabSize;
				indent_size = tab_count + visible_size % indentTabSize;
			}
			int pos = add_area(indent_size);
			int tab_end = pos + tab_count;
			int indent_end = pos + indent_size;
			for (; pos != tab_end; ++pos)
			{
				buffer[pos] = '\t';
			}
			for (; pos != indent_end; ++pos)
			{
				buffer[pos] = ' ';
			}
		}
		
		public virtual void  nl()
		{
			p('\n');
		}
		
		public virtual void  line(int indent_level, string s)
		{
			indent(indent_level); p(s); nl();
		}
		
		public virtual void  erase(int begin, int end)
		{
			Array.Copy(buffer, end, buffer, begin, offset - end);
			offset -= (end - begin);
		}
		
		public override string ToString()
		{
			return new string(buffer, 0, offset);
		}
	}
}