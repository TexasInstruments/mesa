#!/usr/bin/python3

import xml.parsers.expat
import sys
import os
import collections

class Error(Exception):
	def __init__(self, message):
		self.message = message

class Enum(object):
	def __init__(self, name):
		self.name = name
		self.values = []

	def has_name(self, name):
		for (n, value) in self.values:
			if n == name:
				return True
		return False

	def dump(self):
		use_hex = False
		for (name, value) in self.values:
			if value > 0x1000:
				use_hex = True

		print("enum %s {" % self.name)
		for (name, value) in self.values:
			if use_hex:
				print("\t%s = 0x%08x," % (name, value))
			else:
				print("\t%s = %d," % (name, value))
		print("};\n")

	def dump_pack_struct(self):
		pass

class Field(object):
	def __init__(self, name, low, high, shr, type, parser):
		self.name = name
		self.low = low
		self.high = high
		self.shr = shr
		self.type = type

		builtin_types = [ None, "a3xx_regid", "boolean", "uint", "hex", "int", "fixed", "ufixed", "float", "address", "waddress" ]

		maxpos = parser.current_bitsize - 1

		if low < 0 or low > maxpos:
			raise parser.error("low attribute out of range: %d" % low)
		if high < 0 or high > maxpos:
			raise parser.error("high attribute out of range: %d" % high)
		if high < low:
			raise parser.error("low is greater than high: low=%d, high=%d" % (low, high))
		if self.type == "boolean" and not low == high:
			raise parser.error("booleans should be 1 bit fields")
		elif self.type == "float" and not (high - low == 31 or high - low == 15):
			raise parser.error("floats should be 16 or 32 bit fields")
		elif not self.type in builtin_types and not self.type in parser.enums:
			raise parser.error("unknown type '%s'" % self.type)

	def ctype(self, var_name):
		if self.type == None:
			type = "uint32_t"
			val = var_name
		elif self.type == "boolean":
			type = "bool"
			val = var_name
		elif self.type == "uint" or self.type == "hex" or self.type == "a3xx_regid":
			type = "uint32_t"
			val = var_name
		elif self.type == "int":
			type = "int32_t"
			val = var_name
		elif self.type == "fixed":
			type = "float"
			val = "((int32_t)(%s * %d.0))" % (var_name, 1 << self.radix)
		elif self.type == "ufixed":
			type = "float"
			val = "((uint32_t)(%s * %d.0))" % (var_name, 1 << self.radix)
		elif self.type == "float" and self.high - self.low == 31:
			type = "float"
			val = "fui(%s)" % var_name
		elif self.type == "float" and self.high - self.low == 15:
			type = "float"
			val = "_mesa_float_to_half(%s)" % var_name
		elif self.type in [ "address", "waddress" ]:
			type = "uint64_t"
			val = var_name
		else:
			type = "enum %s" % self.type
			val = var_name

		if self.shr > 0:
			val = "(%s >> %d)" % (val, self.shr)

		return (type, val)

def tab_to(name, value):
	tab_count = (68 - (len(name) & ~7)) // 8
	if tab_count <= 0:
		tab_count = 1
	print(name + ('\t' * tab_count) + value)

def mask(low, high):
	return ((0xffffffffffffffff >> (64 - (high + 1 - low))) << low)

def field_name(reg, f):
	if f.name:
		name = f.name.lower()
	else:
		# We hit this path when a reg is defined with no bitset fields, ie.
		# 	<reg32 offset="0x88db" name="RB_BLIT_DST_ARRAY_PITCH" low="0" high="28" shr="6" type="uint"/>
		name = reg.name.lower()

	if (name in [ "double", "float", "int" ]) or not (name[0].isalpha()):
			name = "_" + name

	return name

class Bitset(object):
	def __init__(self, name, template):
		self.name = name
		self.inline = False
		if template:
			self.fields = template.fields[:]
		else:
			self.fields = []

	# Get address field if there is one in the bitset, else return None:
	def get_address_field(self):
		for f in self.fields:
			if f.type in [ "address", "waddress" ]:
				return f
		return None

	def dump_regpair_builder(self, reg):
		print("#ifndef NDEBUG")
		known_mask = 0
		for f in self.fields:
			known_mask |= mask(f.low, f.high)
			if f.type in [ "boolean", "address", "waddress" ]:
				continue
			type, val = f.ctype("fields.%s" % field_name(reg, f))
			print("    assert((%-40s & 0x%08x) == 0);" % (val, 0xffffffff ^ mask(0 , f.high - f.low)))
		print("    assert((%-40s & 0x%08x) == 0);" % ("fields.unknown", known_mask))
		print("#endif\n")

		print("    return (struct fd_reg_pair) {")
		if reg.array:
			print("        .reg = REG_%s(__i)," % reg.full_name)
		else:
			print("        .reg = REG_%s," % reg.full_name)

		print("        .value =")
		for f in self.fields:
			if f.type in [ "address", "waddress" ]:
				continue
			else:
				type, val = f.ctype("fields.%s" % field_name(reg, f))
				print("            (%-40s << %2d) |" % (val, f.low))
		value_name = "dword"
		if reg.bit_size == 64:
			value_name = "qword"
		print("            fields.unknown | fields.%s," % (value_name,))

		address = self.get_address_field()
		if address:
			print("        .bo = fields.bo,")
			print("        .is_address = true,")
			if f.type == "waddress":
				print("        .bo_write = true,")
			print("        .bo_offset = fields.bo_offset,")
			print("        .bo_shift = %d," % address.shr)
			print("        .bo_low = %d," % address.low)

		print("    };")

	def dump_pack_struct(self, reg=None):
		if not reg:
			return

		prefix = reg.full_name

		print("struct %s {" % prefix)
		for f in self.fields:
			if f.type in [ "address", "waddress" ]:
				tab_to("    __bo_type", "bo;")
				tab_to("    uint32_t", "bo_offset;")
				continue
			name = field_name(reg, f)

			type, val = f.ctype("var")

			tab_to("    %s" % type, "%s;" % name)
		if reg.bit_size == 64:
			tab_to("    uint64_t", "unknown;")
			tab_to("    uint64_t", "qword;")
		else:
			tab_to("    uint32_t", "unknown;")
			tab_to("    uint32_t", "dword;")
		print("};\n")

		if reg.array:
			print("static inline struct fd_reg_pair\npack_%s(uint32_t __i, struct %s fields)\n{" %
				  (prefix, prefix))
		else:
			print("static inline struct fd_reg_pair\npack_%s(struct %s fields)\n{" %
				  (prefix, prefix))

		self.dump_regpair_builder(reg)

		print("\n}\n")

		if self.get_address_field():
			skip = ", { .reg = 0 }"
		else:
			skip = ""

		if reg.array:
			print("#define %s(__i, ...) pack_%s(__i, __struct_cast(%s) { __VA_ARGS__ })%s\n" %
				  (prefix, prefix, prefix, skip))
		else:
			print("#define %s(...) pack_%s(__struct_cast(%s) { __VA_ARGS__ })%s\n" %
				  (prefix, prefix, prefix, skip))


	def dump(self, prefix=None):
		if prefix == None:
			prefix = self.name
		for f in self.fields:
			if f.name:
				name = prefix + "_" + f.name
			else:
				name = prefix

			if not f.name and f.low == 0 and f.shr == 0 and not f.type in ["float", "fixed", "ufixed"]:
				pass
			elif f.type == "boolean" or (f.type == None and f.low == f.high):
				tab_to("#define %s" % name, "0x%08x" % (1 << f.low))
			else:
				tab_to("#define %s__MASK" % name, "0x%08x" % mask(f.low, f.high))
				tab_to("#define %s__SHIFT" % name, "%d" % f.low)
				type, val = f.ctype("val")

				print("static inline uint32_t %s(%s val)\n{" % (name, type))
				if f.shr > 0:
					print("\tassert(!(val & 0x%x));" % mask(0, f.shr - 1))
				print("\treturn ((%s) << %s__SHIFT) & %s__MASK;\n}" % (val, name, name))
		print()

class Array(object):
	def __init__(self, attrs, domain, variant):
		if "name" in attrs:
			self.name = attrs["name"]
		else:
			self.name = ""
		self.domain = domain
		self.variant = variant
		self.offset = int(attrs["offset"], 0)
		self.stride = int(attrs["stride"], 0)
		self.length = int(attrs["length"], 0)
		if "usage" in attrs:
			self.usages = attrs["usage"].split(',')
		else:
			self.usages = None

	def dump(self):
		print("#define REG_%s_%s(i0) (0x%08x + 0x%x*(i0))\n" % (self.domain, self.name, self.offset, self.stride))

	def dump_pack_struct(self):
		pass

	def dump_regpair_builder(self):
		pass

class Reg(object):
	def __init__(self, attrs, domain, array, bit_size):
		self.name = attrs["name"]
		self.domain = domain
		self.array = array
		self.offset = int(attrs["offset"], 0)
		self.type = None
		self.bit_size = bit_size
		if array:
			self.name = array.name + "_" + self.name
		self.full_name = self.domain + "_" + self.name

	def dump(self):
		if self.array:
			offset = self.array.offset + self.offset
			print("static inline uint32_t REG_%s(uint32_t i0) { return 0x%08x + 0x%x*i0; }" % (self.full_name, offset, self.array.stride))
		else:
			tab_to("#define REG_%s" % self.full_name, "0x%08x" % self.offset)

		if self.bitset.inline:
			self.bitset.dump(self.full_name)
		print("")

	def dump_pack_struct(self):
		if self.bitset.inline:
			self.bitset.dump_pack_struct(self)

	def dump_regpair_builder(self):
		if self.bitset.inline:
			self.bitset.dump_regpair_builder(self)

class Parser(object):
	def __init__(self):
		self.current_array = None
		self.current_domain = None
		self.current_prefix = None
		self.current_prefix_type = None
		self.current_stripe = None
		self.current_bitset = None
		self.current_bitsize = 32
		# The varset attribute on the domain specifies the enum which
		# specifies all possible hw variants:
		self.current_varset = None
		# Regs that have multiple variants.. we only generated the C++
		# template based struct-packers for these
		self.variant_regs = {}
		# Information in which contexts regs are used, to be used in
		# debug options
		self.usage_regs = collections.defaultdict(list)
		self.bitsets = {}
		self.enums = {}
		self.variants = set()
		self.file = []

	def error(self, message):
		parser, filename = self.stack[-1]
		return Error("%s:%d:%d: %s" % (filename, parser.CurrentLineNumber, parser.CurrentColumnNumber, message))

	def prefix(self, variant=None):
		if self.current_prefix_type == "variant" and variant:
			return variant
		elif self.current_stripe:
			return self.current_stripe + "_" + self.current_domain
		elif self.current_prefix:
			return self.current_prefix + "_" + self.current_domain
		else:
			return self.current_domain

	def parse_field(self, name, attrs):
		try:
			if "pos" in attrs:
				high = low = int(attrs["pos"], 0)
			elif "high" in attrs and "low" in attrs:
				high = int(attrs["high"], 0)
				low = int(attrs["low"], 0)
			else:
				low = 0
				high = self.current_bitsize - 1

			if "type" in attrs:
				type = attrs["type"]
			else:
				type = None

			if "shr" in attrs:
				shr = int(attrs["shr"], 0)
			else:
				shr = 0

			b = Field(name, low, high, shr, type, self)

			if type == "fixed" or type == "ufixed":
				b.radix = int(attrs["radix"], 0)

			self.current_bitset.fields.append(b)
		except ValueError as e:
			raise self.error(e)

	def parse_varset(self, attrs):
		# Inherit the varset from the enclosing domain if not overriden:
		varset = self.current_varset
		if "varset" in attrs:
			varset = self.enums[attrs["varset"]]
		return varset

	def parse_variants(self, attrs):
		if not "variants" in attrs:
				return None
		variant = attrs["variants"].split(",")[0]
		if "-" in variant:
			variant = variant[:variant.index("-")]

		varset = self.parse_varset(attrs)

		assert varset.has_name(variant)

		return variant

	def add_all_variants(self, reg, attrs, parent_variant):
		# TODO this should really handle *all* variants, including dealing
		# with open ended ranges (ie. "A2XX,A4XX-") (we have the varset
		# enum now to make that possible)
		variant = self.parse_variants(attrs)
		if not variant:
			variant = parent_variant

		if reg.name not in self.variant_regs:
			self.variant_regs[reg.name] = {}
		else:
			# All variants must be same size:
			v = next(iter(self.variant_regs[reg.name]))
			assert self.variant_regs[reg.name][v].bit_size == reg.bit_size

		self.variant_regs[reg.name][variant] = reg

	def add_all_usages(self, reg, usages):
		if not usages:
			return

		for usage in usages:
			self.usage_regs[usage].append(reg)

		self.variants.add(reg.domain)

	def do_validate(self, schemafile):
		try:
			from lxml import etree

			parser, filename = self.stack[-1]
			dirname = os.path.dirname(filename)

			# we expect this to look like <namespace url> schema.xsd.. I think
			# technically it is supposed to be just a URL, but that doesn't
			# quite match up to what we do.. Just skip over everything up to
			# and including the first whitespace character:
			schemafile = schemafile[schemafile.rindex(" ")+1:]

			# this is a bit cheezy, but the xml file to validate could be
			# in a child director, ie. we don't really know where the schema
			# file is, the way the rnn C code does.  So if it doesn't exist
			# just look one level up
			if not os.path.exists(dirname + "/" + schemafile):
				schemafile = "../" + schemafile

			if not os.path.exists(dirname + "/" + schemafile):
				raise self.error("Cannot find schema for: " + filename)

			xmlschema_doc = etree.parse(dirname + "/" + schemafile)
			xmlschema = etree.XMLSchema(xmlschema_doc)

			xml_doc = etree.parse(filename)
			if not xmlschema.validate(xml_doc):
				error_str = str(xmlschema.error_log.filter_from_errors()[0])
				raise self.error("Schema validation failed for: " + filename + "\n" + error_str)
		except ImportError:
			print("lxml not found, skipping validation", file=sys.stderr)

	def do_parse(self, filename):
		file = open(filename, "rb")
		parser = xml.parsers.expat.ParserCreate()
		self.stack.append((parser, filename))
		parser.StartElementHandler = self.start_element
		parser.EndElementHandler = self.end_element
		parser.ParseFile(file)
		self.stack.pop()
		file.close()

	def parse(self, rnn_path, filename):
		self.path = rnn_path
		self.stack = []
		self.do_parse(filename)

	def parse_reg(self, attrs, bit_size):
		self.current_bitsize = bit_size
		if "type" in attrs and attrs["type"] in self.bitsets:
			bitset = self.bitsets[attrs["type"]]
			if bitset.inline:
				self.current_bitset = Bitset(attrs["name"], bitset)
				self.current_bitset.inline = True
			else:
				self.current_bitset = bitset
		else:
			self.current_bitset = Bitset(attrs["name"], None)
			self.current_bitset.inline = True
			if "type" in attrs:
				self.parse_field(None, attrs)

		variant = self.parse_variants(attrs)
		if not variant and self.current_array:
			variant = self.current_array.variant

		self.current_reg = Reg(attrs, self.prefix(variant), self.current_array, bit_size)
		self.current_reg.bitset = self.current_bitset

		if len(self.stack) == 1:
			self.file.append(self.current_reg)

		if variant is not None:
			self.add_all_variants(self.current_reg, attrs, variant)

		usages = None
		if "usage" in attrs:
			usages = attrs["usage"].split(',')
		elif self.current_array:
			usages = self.current_array.usages

		self.add_all_usages(self.current_reg, usages)

	def start_element(self, name, attrs):
		if name == "import":
			filename = attrs["file"]
			self.do_parse(os.path.join(self.path, filename))
		elif name == "domain":
			self.current_domain = attrs["name"]
			if "prefix" in attrs:
				self.current_prefix = self.parse_variants(attrs)
				self.current_prefix_type = attrs["prefix"]
			else:
				self.current_prefix = None
				self.current_prefix_type = None
			if "varset" in attrs:
				self.current_varset = self.enums[attrs["varset"]]
		elif name == "stripe":
			self.current_stripe = self.parse_variants(attrs)
		elif name == "enum":
			self.current_enum_value = 0
			self.current_enum = Enum(attrs["name"])
			self.enums[attrs["name"]] = self.current_enum
			if len(self.stack) == 1:
				self.file.append(self.current_enum)
		elif name == "value":
			if "value" in attrs:
				value = int(attrs["value"], 0)
			else:
				value = self.current_enum_value
			self.current_enum.values.append((attrs["name"], value))
		elif name == "reg32":
			self.parse_reg(attrs, 32)
		elif name == "reg64":
			self.parse_reg(attrs, 64)
		elif name == "array":
			self.current_bitsize = 32
			variant = self.parse_variants(attrs)
			self.current_array = Array(attrs, self.prefix(variant), variant)
			if len(self.stack) == 1:
				self.file.append(self.current_array)
		elif name == "bitset":
			self.current_bitset = Bitset(attrs["name"], None)
			if "inline" in attrs and attrs["inline"] == "yes":
				self.current_bitset.inline = True
			self.bitsets[self.current_bitset.name] = self.current_bitset
			if len(self.stack) == 1 and not self.current_bitset.inline:
				self.file.append(self.current_bitset)
		elif name == "bitfield" and self.current_bitset:
			self.parse_field(attrs["name"], attrs)
		elif name == "database":
			self.do_validate(attrs["xsi:schemaLocation"])

	def end_element(self, name):
		if name == "domain":
			self.current_domain = None
			self.current_prefix = None
			self.current_prefix_type = None
		elif name == "stripe":
			self.current_stripe = None
		elif name == "bitset":
			self.current_bitset = None
		elif name == "reg32":
			self.current_reg = None
		elif name == "array":
			self.current_array = None
		elif name == "enum":
			self.current_enum = None

	def dump_reg_usages(self):
		d = collections.defaultdict(list)
		for usage, regs in self.usage_regs.items():
			for reg in regs:
				variants = self.variant_regs.get(reg.name)
				if variants:
					for variant, vreg in variants.items():
						if reg == vreg:
							d[(usage, variant)].append(reg)
				else:
					for variant in self.variants:
						d[(usage, variant)].append(reg)

		print("#ifdef __cplusplus")

		for usage, regs in self.usage_regs.items():
			print("template<chip CHIP> constexpr inline uint16_t %s_REGS[] = {};" % (usage.upper()))

		for (usage, variant), regs in d.items():
			offsets = []

			for reg in regs:
				if reg.array:
					for i in range(reg.array.length):
						offsets.append(reg.array.offset + reg.offset + i * reg.array.stride)
						if reg.bit_size == 64:
							offsets.append(offsets[-1] + 1)
				else:
					offsets.append(reg.offset)
					if reg.bit_size == 64:
						offsets.append(offsets[-1] + 1)

			offsets.sort()

			print("template<> constexpr inline uint16_t %s_REGS<%s>[] = {" % (usage.upper(), variant))
			for offset in offsets:
				print("\t%s," % hex(offset))
			print("};")

		print("#endif")

	def dump(self):
		enums = []
		bitsets = []
		regs = []
		for e in self.file:
			if isinstance(e, Enum):
				enums.append(e)
			elif isinstance(e, Bitset):
				bitsets.append(e)
			else:
				regs.append(e)

		for e in enums + bitsets + regs:
			e.dump()

		self.dump_reg_usages()

	def dump_reg_variants(self, regname, variants):
		# Don't bother for things that only have a single variant:
		if len(variants) == 1:
			return
		print("#ifdef __cplusplus")
		print("struct __%s {" % regname)
		# TODO be more clever.. we should probably figure out which
		# fields have the same type in all variants (in which they
		# appear) and stuff everything else in a variant specific
		# sub-structure.
		seen_fields = []
		bit_size = 32
		array = False
		address = None
		for variant in variants.keys():
			print("    /* %s fields: */" % variant)
			reg = variants[variant]
			bit_size = reg.bit_size
			array = reg.array
			for f in reg.bitset.fields:
				fld_name = field_name(reg, f)
				if fld_name in seen_fields:
					continue
				seen_fields.append(fld_name)
				name = fld_name.lower()
				if f.type in [ "address", "waddress" ]:
					if address:
						continue
					address = f
					tab_to("    __bo_type", "bo;")
					tab_to("    uint32_t", "bo_offset;")
					continue
				type, val = f.ctype("var")
				tab_to("    %s" %type, "%s;" %name)
		print("    /* fallback fields: */")
		if bit_size == 64:
			tab_to("    uint64_t", "unknown;")
			tab_to("    uint64_t", "qword;")
		else:
			tab_to("    uint32_t", "unknown;")
			tab_to("    uint32_t", "dword;")
		print("};")
		# TODO don't hardcode the varset enum name
		varenum = "chip"
		print("template <%s %s>" % (varenum, varenum.upper()))
		print("static inline struct fd_reg_pair")
		xtra = ""
		xtravar = ""
		if array:
			xtra = "int __i, "
			xtravar = "__i, "
		print("__%s(%sstruct __%s fields) {" % (regname, xtra, regname))
		for variant in variants.keys():
			print("  if (%s == %s) {" % (varenum.upper(), variant))
			reg = variants[variant]
			reg.dump_regpair_builder()
			print("  } else")
		print("    assert(!\"invalid variant\");")
		print("}")

		if bit_size == 64:
			skip = ", { .reg = 0 }"
		else:
			skip = ""

		print("#define %s(VARIANT, %s...) __%s<VARIANT>(%s{__VA_ARGS__})%s" % (regname, xtravar, regname, xtravar, skip))
		print("#endif /* __cplusplus */")

	def dump_structs(self):
		for e in self.file:
			e.dump_pack_struct()

		for regname in self.variant_regs:
			self.dump_reg_variants(regname, self.variant_regs[regname])


def main():
	p = Parser()
	rnn_path = sys.argv[1]
	xml_file = sys.argv[2]
	if len(sys.argv) > 3 and sys.argv[3] == '--pack-structs':
		do_structs = True
		guard = str.replace(os.path.basename(xml_file), '.', '_').upper() + '_STRUCTS'
	else:
		do_structs = False
		guard = str.replace(os.path.basename(xml_file), '.', '_').upper()

	print("#ifndef %s\n#define %s\n" % (guard, guard))

	print()
	print("#include <assert.h>")
	print()

	print("#ifdef __cplusplus")
	print("#define __struct_cast(X)")
	print("#else")
	print("#define __struct_cast(X) (struct X)")
	print("#endif")

	try:
		p.parse(rnn_path, xml_file)
	except Error as e:
		print(e, file=sys.stderr)
		exit(1)

	if do_structs:
		p.dump_structs()
	else:
		p.dump()

	print("\n#endif /* %s */" % guard)

if __name__ == '__main__':
	main()
