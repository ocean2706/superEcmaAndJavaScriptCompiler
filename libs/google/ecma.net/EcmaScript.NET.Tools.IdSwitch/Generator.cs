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

using EcmaScript.NET;

namespace EcmaScript.NET.Tools.IdSwitch
{
	
	public class Generator
	{

        virtual public string SourceFileName
		{
			get
			{
				return source_file;
			}
			
			set
			{
				source_file = value;
			}
			
		}
		
		
		internal string v_switch_label = "L0";
		internal string v_switch_label_end = "EL0";
		internal string v_label = "L";
		internal string v_s = "s";
		internal string v_c = "c";
		internal string v_guess = "X";
		internal string v_id = "id";
		internal string v_length_suffix = "_length";
		
		internal int use_if_threshold = 3;
		internal int char_tail_test_threshold = 2;
		
		private Pair[] pairs;
		private string default_value;
		private int[] columns;
		private bool c_was_defined;

        internal CodePrinter P = new CodePrinter();

		private string source_file;
		
		public virtual void  generateSwitch(string[] pairs, string default_value)
		{
			int N = pairs.Length / 2;
			Pair[] id_pairs = new Pair[N];
			for (int i = 0; i != N; ++i)
			{
				id_pairs[i] = new Pair(pairs[2 * i], pairs[2 * i + 1]);
			}
			generateSwitch(id_pairs, default_value);
		}
		
		public virtual void  generateSwitch(Pair[] pairs, string default_value)
		{
			int begin = 0;
			int end = pairs.Length;
			if (begin == end)
			{
				return ;
			}
			this.pairs = pairs;
			this.default_value = default_value;
			
			generate_body(begin, end, 2);
		}
		
		private void  generate_body(int begin, int end, int indent_level)
		{
			// Disable warning "Label is never referenced"			
			P.indent(indent_level);
			P.p(v_switch_label);
			P.p(": ");
			P.p("{ ");
			P.p(v_id); P.p(" = "); P.p(default_value);
			P.p("; string "); P.p(v_guess); P.p(" = null;");
			
			c_was_defined = false;
			int c_def_begin = P.Offset;
			P.p(" int "); P.p(v_c); P.p(';');
			int c_def_end = P.Offset;
			P.nl();
			
			generate_length_switch(begin, end, indent_level + 1);
			
			if (!c_was_defined)
			{
				P.erase(c_def_begin, c_def_end);
			}
			
			P.indent(indent_level + 1);
			P.p("if ("); P.p(v_guess); P.p("!=null && ");
			P.p(v_guess); P.p("!="); P.p(v_s);
			P.p(" && !"); P.p(v_guess); P.p(".Equals("); P.p(v_s); P.p(")) ");
			P.p(v_id); P.p(" = "); P.p(default_value); P.p(";"); P.nl();
			P.line(indent_level, "}");
			P.line(indent_level, v_switch_label_end + ":");
		}
		
		private void  generate_length_switch(int begin, int end, int indent_level)
		{
			
			sort_pairs(begin, end, - 1);
			
			check_all_is_different(begin, end);
			
			int lengths_count = count_different_lengths(begin, end);
			
			columns = new int[pairs[end - 1].Value.Length];
			
			bool use_if;
			if (lengths_count <= use_if_threshold)
			{
				use_if = true;
				if (lengths_count != 1)
				{
					P.indent(indent_level);
					P.p("int "); P.p(v_s); P.p(v_length_suffix);
					P.p(" = "); P.p(v_s); P.p(".Length;");
					P.nl();
				}
			}
			else
			{
				use_if = false;
				P.indent(indent_level);
				P.p(v_label); P.p(": switch (");
				P.p(v_s); P.p(".Length) {");
				P.nl();
			}
			
			int same_length_begin = begin;
			int cur_l = pairs[begin].Value.Length, l = 0;
			for (int i = begin; ; )
			{
				++i;
				if (i == end || (l = pairs[i].Value.Length) != cur_l)
				{
					int next_indent;
					if (use_if)
					{
						P.indent(indent_level);
						if (same_length_begin != begin)
						{
							P.p("else ");
						}
						P.p("if (");
						if (lengths_count == 1)
						{
							P.p(v_s); P.p(".Length==");
						}
						else
						{
							P.p(v_s); P.p(v_length_suffix); P.p("==");
						}
						P.p(cur_l);
						P.p(") {");
						next_indent = indent_level + 1;
					}
					else
					{
						P.indent(indent_level);
						P.p("case "); P.p(cur_l); P.p(":");
						next_indent = indent_level + 1;
					}
					generate_letter_switch(same_length_begin, i, next_indent, !use_if, use_if);
					if (use_if)
					{
						P.p("}"); P.nl();
					}
					else
					{
						P.p("break"); /*P.p(v_label);*/ P.p(";"); P.nl();
					}
					
					if (i == end)
					{
						break;
					}
					same_length_begin = i;
					cur_l = l;
				}
			}
			
			if (!use_if)
			{
				P.indent(indent_level); P.p("}"); P.nl();
			}
		}
		
		private void  generate_letter_switch(int begin, int end, int indent_level, bool label_was_defined, bool inside_if)
		{
			int L = pairs[begin].Value.Length;
			
			for (int i = 0; i != L; ++i)
			{
				columns[i] = i;
			}
			
			generate_letter_switch_r(begin, end, L, indent_level, label_was_defined, inside_if);
		}
		
		
		private bool generate_letter_switch_r(int begin, int end, int L, int indent_level, bool label_was_defined, bool inside_if)
		{
			bool next_is_unreachable = false;
			if (begin + 1 == end)
			{
				P.p(' ');
				Pair pair = pairs[begin];
				if (L > char_tail_test_threshold)
				{
					P.p(v_guess); P.p("="); P.qstring(pair.Value); P.p(";");
					P.p(v_id); P.p("="); P.p(pair.Id); P.p(";");
				}
				else
				{
					if (L == 0)
					{
						next_is_unreachable = true;
						P.p(v_id); P.p("="); P.p(pair.Id);
						P.p("; goto "); P.p(v_switch_label_end); P.p(";");						
					}
					else
					{
						P.p("if (");
						int column = columns[0];
						P.p(v_s); P.p("["); P.p(column); P.p("]==");
						P.qchar(pair.Value[column]);
						for (int i = 1; i != L; ++i)
						{
							P.p(" && ");
							column = columns[i];
							P.p(v_s); P.p("["); P.p(column); P.p("]==");
							P.qchar(pair.Value[column]);
						}
						P.p(") {");
						P.p(v_id); P.p("="); P.p(pair.Id);
						P.p("; goto "); P.p(v_switch_label_end); P.p(";}");						
					}
				}
				P.p(' ');
				return next_is_unreachable;
			}
			
			int max_column_index = find_max_different_column(begin, end, L);
			int max_column = columns[max_column_index];
			int count = count_different_chars(begin, end, max_column);
			
			columns[max_column_index] = columns[L - 1];
			
			if (inside_if)
			{
				P.nl(); P.indent(indent_level);
			}
			else
			{
				P.p(' ');
			}
			
			bool use_if;
			if (count <= use_if_threshold)
			{
				use_if = true;
				c_was_defined = true;
				P.p(v_c); P.p("="); P.p(v_s);
				P.p("["); P.p(max_column); P.p("];");
			}
			else
			{
				use_if = false;
				if (!label_was_defined)
				{
					label_was_defined = true;
					P.p(v_label); P.p(": ");
				}
				P.p("switch ("); P.p(v_s);
				P.p("["); P.p(max_column); P.p("]) {");
			}
			
			int same_char_begin = begin;
			int cur_ch = pairs[begin].Value[max_column], ch = 0;
			for (int i = begin; ; )
			{
				++i;
				if (i == end || (ch = pairs[i].Value[max_column]) != cur_ch)
				{
					int next_indent;
					if (use_if)
					{
						P.nl(); P.indent(indent_level);
						if (same_char_begin != begin)
						{
							P.p("else ");
						}
						P.p("if ("); P.p(v_c); P.p("==");
						P.qchar(cur_ch); P.p(") {");
						next_indent = indent_level + 1;
					}
					else
					{
						P.nl(); P.indent(indent_level);
						P.p("case "); P.qchar(cur_ch); P.p(":");
						next_indent = indent_level + 1;
					}
					bool after_unreachable = generate_letter_switch_r(same_char_begin, i, L - 1, next_indent, label_was_defined, use_if);
					if (use_if)
					{
						P.p("}");
					}
					else
					{
						if (!after_unreachable)
						{
							P.p("break");/* P.p(v_label);*/P.p(";");
						}
					}
					if (i == end)
					{
						break;
					}
					same_char_begin = i;
					cur_ch = ch;
				}
			}
			
			if (use_if)
			{
				P.nl();
				if (inside_if)
				{
					P.indent(indent_level - 1);
				}
				else
				{
					P.indent(indent_level);
				}
			}
			else
			{
				P.nl(); P.indent(indent_level); P.p("}");
				if (inside_if)
				{
					P.nl(); P.indent(indent_level - 1);
				}
				else
				{
					P.p(' ');
				}
			}
			
			columns[max_column_index] = max_column;
			
			return next_is_unreachable;
		}
		
		
		private int count_different_lengths(int begin, int end)
		{
			int lengths_count = 0;
			int cur_l = - 1;
			for (; begin != end; ++begin)
			{
				int l = pairs[begin].Value.Length;
				if (cur_l != l)
				{
					++lengths_count; cur_l = l;
				}
			}
			return lengths_count;
		}
		
		private int find_max_different_column(int begin, int end, int L)
		{
			int max_count = 0;
			int max_index = 0;
			
			for (int i = 0; i != L; ++i)
			{
				int column = columns[i];
				sort_pairs(begin, end, column);
				int count = count_different_chars(begin, end, column);
				if (count == end - begin)
				{
					return i;
				}
				if (max_count < count)
				{
					max_count = count;
					max_index = i;
				}
			}
			
			if (max_index != L - 1)
			{
				sort_pairs(begin, end, columns[max_index]);
			}
			
			return max_index;
		}
		
		private int count_different_chars(int begin, int end, int column)
		{
			int chars_count = 0;
			int cur_ch = - 1;
			for (; begin != end; ++begin)
			{
				int ch = pairs[begin].Value[column];
				if (ch != cur_ch)
				{
					++chars_count; cur_ch = ch;
				}
			}
			return chars_count;
		}
		
		private void  check_all_is_different(int begin, int end)
		{
			if (begin != end)
			{
				Pair prev = pairs[begin];
				while (++begin != end)
				{
					Pair current = pairs[begin];
					if (prev.Value.Equals(current.Value))
					{
						throw new ApplicationException();
					}
					prev = current;
				}
			}
		}
		
		private void  sort_pairs(int begin, int end, int comparator)
		{
			heap4Sort(pairs, begin, end - begin, comparator);
		}
		
		private static bool bigger(Pair a, Pair b, int comparator)
		{
			if (comparator < 0)
			{
				// For length selection switch it is enough to compare just length,
				// but to detect same strings full comparison is essential
				//return a.Id.Length > b.Id.Length;
				int diff = a.Value.Length - b.Value.Length;
				if (diff != 0)
				{
					return diff > 0;
				}
				return String.CompareOrdinal(a.Value, b.Value) > 0;
			}
			else
			{
				return a.Value[comparator] > b.Value[comparator];
			}
		}
		
		private static void  heap4Sort(Pair[] array, int offset, int size, int comparator)
		{
			if (size <= 1)
			{
				return ;
			}
			makeHeap4(array, offset, size, comparator);
			while (size > 1)
			{
				--size;
				Pair v1 = array[offset + size];
				Pair v2 = array[offset + 0];
				array[offset + size] = v2;
				array[offset + 0] = v1;
				heapify4(array, offset, size, 0, comparator);
			}
		}
		
		private static void  makeHeap4(Pair[] array, int offset, int size, int comparator)
		{
			for (int i = ((size + 2) >> 2); i != 0; )
			{
				--i;
				heapify4(array, offset, size, i, comparator);
			}
		}
		
		private static void  heapify4(Pair[] array, int offset, int size, int i, int comparator)
		{
			int new_i1, new_i2, new_i3;
			Pair i_val = array[offset + i];
			for (; ; )
			{
				int base_Renamed = (i << 2);
				new_i1 = base_Renamed | 1;
				new_i2 = base_Renamed | 2;
				new_i3 = base_Renamed | 3;
				int new_i4 = base_Renamed + 4;
				if (new_i4 >= size)
				{
					break;
				}
				Pair val1 = array[offset + new_i1];
				Pair val2 = array[offset + new_i2];
				Pair val3 = array[offset + new_i3];
				Pair val4 = array[offset + new_i4];
				if (bigger(val2, val1, comparator))
				{
					val1 = val2; new_i1 = new_i2;
				}
				if (bigger(val4, val3, comparator))
				{
					val3 = val4; new_i3 = new_i4;
				}
				if (bigger(val3, val1, comparator))
				{
					val1 = val3; new_i1 = new_i3;
				}
				if (bigger(i_val, val1, comparator))
				{
					return ;
				}
				array[offset + i] = val1;
				array[offset + new_i1] = i_val;
				i = new_i1;
			}
			if (new_i1 < size)
			{
				Pair val1 = array[offset + new_i1];
				if (new_i2 != size)
				{
					Pair val2 = array[offset + new_i2];
					if (bigger(val2, val1, comparator))
					{
						val1 = val2; new_i1 = new_i2;
					}
					if (new_i3 != size)
					{
						Pair val3 = array[offset + new_i3];
						if (bigger(val3, val1, comparator))
						{
							val1 = val3; new_i1 = new_i3;
						}
					}
				}
				if (bigger(val1, i_val, comparator))
				{
					array[offset + i] = val1;
					array[offset + new_i1] = i_val;
				}
			}
		}
	}
}