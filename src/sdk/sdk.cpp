#include "sdk/sdk.h"
#include <filesystem>
#include <set>
#include <string_view>

namespace {
    using namespace std::string_view_literals;

    constexpr std::string_view kOutDirName = "sdk"sv;
    constexpr std::initializer_list<std::string_view> kIncludePaths = {"<cstdint>"sv, "\"!GlobalTypes.hpp\""sv};

    constexpr std::initializer_list<fnv32::hash> kStringMetadataEntries = {
        FNV32("MNetworkChangeCallback"),  FNV32("MPropertyFriendlyName"), FNV32("MPropertyDescription"),
        FNV32("MPropertyAttributeRange"), FNV32("MPropertyStartGroup"),   FNV32("MPropertyAttributeChoiceName"),
        FNV32("MPropertyGroupName"),      FNV32("MNetworkUserGroup"),     FNV32("MNetworkAlias"),
        FNV32("MNetworkTypeAlias"),       FNV32("MNetworkSerializer"),    FNV32("MPropertyAttributeEditor"),
        FNV32("MPropertySuppressExpr"),   FNV32("MKV3TransferName"),
    };

    constexpr std::initializer_list<fnv32::hash> kIntegerMetadataEntries = {
        FNV32("MNetworkVarEmbeddedFieldOffsetDelta"),
        FNV32("MNetworkBitCount"),
        FNV32("MNetworkPriority"),
        FNV32("MPropertySortPriority"),
    };

    constexpr std::initializer_list<fnv32::hash> kFloatMetadataEntries = {
        FNV32("MNetworkMinValue"),
        FNV32("MNetworkMaxValue"),
    };

    inline bool ends_with(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
    }
} // namespace

namespace sdk {
    namespace {
        __forceinline void PrintClassInfo(codegen::generator_t::self_ref builder, std::int16_t alignment, std::int16_t size) {
            builder.comment(std::format("Alignment: {}", alignment)).comment(std::format("Size: {:#x}", size));
        }

        void AssembleEnums(codegen::generator_t::self_ref builder, CUtlTSHash<CSchemaEnumBinding*> enums) {
            for (auto schema_enum_binding : enums.GetElements()) {
                // @note: @es3n1n: get type name by align size
                //
                const auto get_type_name = [schema_enum_binding]() [[msvc::forceinline]] {
                    std::string type_storage;

                    switch (schema_enum_binding->m_align_) {
                    case 1:
                        type_storage = "uint8_t";
                        break;
                    case 2:
                        type_storage = "uint16_t";
                        break;
                    case 4:
                        type_storage = "uint32_t";
                        break;
                    case 8:
                        type_storage = "uint64_t";
                        break;
                    default:
                        type_storage = "INVALID_TYPE";
                    }

                    return type_storage;
                };

                // @todo: @es3n1n: assemble flags
                //
                // if (schema_enum_binding->m_flags_) out.print("// Flags: MEnumFlagsWithOverlappingBits\n");

                // @note: @es3n1n: print meta info
                //
                PrintClassInfo(builder, schema_enum_binding->m_align_, schema_enum_binding->m_size_);

                // @note: @es3n1n: begin enum class
                //
                builder.begin_enum_class(schema_enum_binding->m_binding_name_, get_type_name());

                // @note: @es3n1n: assemble enum items
                //
                for (auto l = 0; l < schema_enum_binding->m_size_; l++) {
                    auto& field = schema_enum_binding->m_enum_info_[l];

                    builder.enum_item(field.m_name, field.m_value == std::numeric_limits<std::size_t>::max() ? -1 : field.m_value);
                }

                // @note: @es3n1n: we are done with this enum
                //
                builder.end_enum_class();
            }
        }

        void AssembleClasses(CSchemaSystemTypeScope* current, codegen::generator_t::self_ref builder, CUtlTSHash<CSchemaClassBinding*> classes) {
            struct class_t {
                CSchemaClassInfo* target_;
                std::set<CSchemaClassInfo*> refs_;

                CSchemaClassInfo* GetParent() {
                    if (!target_->m_schema_parent)
                        return nullptr;

                    return target_->m_schema_parent->m_class;
                }

                void AddRefToClass(CSchemaType* type) {
                    if (type->type_category == Schema_DeclaredClass) {
                        refs_.insert(type->m_class_info);
                        return;
                    }

                    // auto ptr = type->GetRefClass();
                    // if (ptr && ptr->type_category == Schema_DeclaredClass)
                    // {
                    // 	refs_.insert(ptr->m_class_info);
                    // 	return;
                    // }
                }

                bool IsDependsOn(const class_t& other) {
                    // if current class inherit other.
                    auto parent = this->GetParent();
                    if (parent == other.target_)
                        return true;

                    // if current class contains ref to other.
                    if (this->refs_.contains(other.target_))
                        return true;

                    // otherwise, order doesn`t matter.
                    return false;
                }

                SchemaClassFieldData_t* GetFirstField() {
                    if (target_->m_align)
                        return &target_->m_fields[0];
                    return nullptr;
                }

                // @note: @es3n1n: Returns the struct size without its parent's size
                //
                std::ptrdiff_t ClassSizeWithoutParent() {
                    if (CSchemaClassInfo* class_parent = this->GetParent(); class_parent)
                        return this->target_->m_size - class_parent->m_size;
                    return this->target_->m_size;
                }
            };

            // @note: @soufiw:
            // sort all classes based on refs and inherit, and then print it.
            // ==================
            std::list<class_t> classes_to_dump;
            bool did_forward_decls = false;

            for (const auto schema_class_binding : classes.GetElements()) {
                CSchemaClassInfo* class_info;
                current->FindDeclaredClass(&class_info, schema_class_binding->m_binary_name);

                auto& class_dump = classes_to_dump.emplace_back();
                class_dump.target_ = class_info;

                for (auto k = 0; k < class_info->m_align; k++) {
                    const auto field = &class_info->m_fields[k];
                    if (!field)
                        continue;

                    // forward declare all classes.
                    // @todo: maybe we need to forward declare only pointers to classes?
                    auto ptr = field->m_type->GetRefClass();
                    auto actual_type = ptr ? ptr : field->m_type;

                    if (actual_type->type_category == Schema_DeclaredClass) {
                        builder.forward_declartion(actual_type->m_name_);
                        did_forward_decls = true;
                    }

                    class_dump.AddRefToClass(field->m_type);
                }
            }

            if (did_forward_decls)
                builder.next_line();

            bool did_change = false;
            do {
                did_change = false;

                // swap until we done.
                for (auto first = classes_to_dump.begin(); first != classes_to_dump.end(); ++first) {
                    bool second_below_first = false;

                    for (auto second = classes_to_dump.begin(); second != classes_to_dump.end(); ++second) {
                        if (second == first) {
                            second_below_first = true;
                            continue;
                        }

                        // swap if second class below first, and first depends on second.
                        bool first_depend = first->IsDependsOn(*second);

                        // swap if first class below second, and second depends on first.
                        bool second_depend = second->IsDependsOn(*first);

                        if (first_depend && second_depend) {
                            // classes depends on each other, forward declare them.
                            // @todo: verify that cyclic dependencies is a pointers.
                            continue;
                        }

                        bool swap = second_below_first ? first_depend : second_depend;
                        if (swap) {
                            std::iter_swap(first, second);
                            did_change = true;
                        }
                    }
                }
            } while (did_change);
            // ==================

            // returns {type_name, array_sizes}
            auto parse_array = [&](CSchemaType* type) -> std::pair<std::string, std::vector<std::size_t>> {
                auto ptr = type->GetRefClass();
                auto actual_type = ptr ? ptr : type;

                std::string base_type;
                std::vector<std::size_t> sizes;

                if (actual_type->type_category == Schema_FixedArray) {
                    // dump all sizes.
                    auto schema = actual_type;
                    while (true) {
                        sizes.emplace_back(schema->m_array_.array_size);
                        schema = schema->m_array_.element_type_;

                        if (schema->type_category != Schema_FixedArray) {
                            base_type = schema->m_name_;
                            break;
                        }
                    }
                }

                return {base_type, sizes};
            };

            // returns {type_name, array_sizes}
            auto get_type = [&](CSchemaType* type) -> std::pair<std::string, std::vector<std::size_t>> {
                auto [type_name, mods] = parse_array(type);

                assert((!type_name.empty() && !mods.empty()) || (type_name.empty() && mods.empty()));

                if (!type_name.empty() && !mods.empty())
                    return {type_name, mods};

                return {type->m_name_, {}};
            };

            for (auto& class_dump : classes_to_dump) {
                // @note: @es3n1n: get class info, assemble it
                //
                const auto class_parent = class_dump.GetParent();
                const auto class_info = class_dump.target_;
                const auto is_struct = ends_with(class_info->m_name, "_t");
                PrintClassInfo(builder, class_info->m_align, class_info->m_size);

                // @note: @es3n1n: get parent name
                //
                std::string parent_cls_name;
                if (auto parent = class_info->m_schema_parent ? class_info->m_schema_parent->m_class : nullptr; parent)
                    parent_cls_name = parent->m_name;

                // @note: @es3n1n: start class
                //
                if (is_struct)
                    builder.begin_struct_with_base_type(class_info->m_name, parent_cls_name, "");
                else
                    builder.begin_class_with_base_type(class_info->m_name, parent_cls_name, "");

                // @note: @es3n1n: field assembling state
                //
                struct {
                    std::size_t last_field_size = 0ull;
                    std::size_t last_field_offset = 0ull;
                    bool assembling_bitfield = false;
                    std::size_t total_bits_count_in_union = 0ull;

                    std::ptrdiff_t collision_end_offset = 0ull; // @fixme: @es3n1n: todo proper collision fix and remove this var
                } state;

                // @note: @es3n1n: if we need to pad first field or if there's no fields in this class
                // and we need to properly pad it to make sure its size is the same as we expect it
                //
                std::optional<std::ptrdiff_t> first_field_offset = std::nullopt;
                if (const auto first_field = class_dump.GetFirstField(); first_field)
                    first_field_offset = first_field->m_single_inheritance_offset;

                const std::ptrdiff_t parent_class_size = class_parent ? class_parent->m_size : 0;

                std::ptrdiff_t expected_pad_size = first_field_offset.value_or(class_dump.ClassSizeWithoutParent());
                if (expected_pad_size) // @note: @es3n1n: if there's a pad size we should account the parent class size
                    expected_pad_size -= parent_class_size;

                // @note: @es3n1n: and finally insert a pad
                //
                if (expected_pad_size > 0) // @fixme: @es3n1n: this is wrong, i probably should check for collisions instead
                    builder.access_modifier("private")
                        .struct_padding(parent_class_size, expected_pad_size, false, true)
                        .reset_tabs_count()
                        .comment(std::format("{:#x}", parent_class_size))
                        .restore_tabs_count();

                // @todo: @es3n1n: if for some mysterious reason this class describes fields
                // of the base class we should handle it too.
                if (class_parent && first_field_offset.has_value() && first_field_offset.value() < class_parent->m_size) {
                    builder.comment(
                        std::format("Collision detected({:#x}->{:#x}), output may be wrong.", first_field_offset.value_or(0), class_parent->m_size));
                    state.collision_end_offset = class_parent->m_size;
                }

                // @note: @es3n1n: begin public members
                //
                builder.access_modifier("public");

                for (auto k = 0; k < class_info->m_align; k++) {
                    const auto field = &class_info->m_fields[k];
                    if (!field)
                        continue;

                    // @fixme: @es3n1n: todo proper collision fix and remove this block
                    if (state.collision_end_offset && field->m_single_inheritance_offset < state.collision_end_offset) {
                        builder.comment(std::format("Skipped field \"{}\" @ {:#x} because of the struct collision", field->m_name,
                                                    field->m_single_inheritance_offset));
                        continue;
                    }

                    // @note: @es3n1n: some more utils
                    //
                    auto get_metadata_type = [&](SchemaMetadataEntryData_t metadata_entry) -> std::string {
                        std::string value;

                        const auto value_hash_name = fnv32::hash_runtime(metadata_entry.m_name);

                        // clang-format off
                        if (std::find(kStringMetadataEntries.begin(), kStringMetadataEntries.end(), value_hash_name) != kStringMetadataEntries.end())
                            value = metadata_entry.m_value->m_sz_value;
                        else if (std::find(kIntegerMetadataEntries.begin(), kIntegerMetadataEntries.end(), value_hash_name) != kIntegerMetadataEntries.end())
                            value = std::to_string(metadata_entry.m_value->m_n_value);
                        else if (std::find(kFloatMetadataEntries.begin(), kFloatMetadataEntries.end(), value_hash_name) != kFloatMetadataEntries.end())
                            value = std::to_string(metadata_entry.m_value->m_f_value);
                        // clang-format on

                        return value;
                    };

                    // @note: @es3n1n: obtaining size
                    //
                    int field_size = 0;
                    if (!field->m_type->GetSize(&field_size)) // @note: @es3n1n: should happen if we are attempting to get a size of the bitfield
                        field_size = 0;

                    // @note: @es3n1n: parsing type
                    //
                    const auto [type, mod] = get_type(field->m_type);
                    const auto var_info = field_parser::parse(type, field->m_name, mod);

                    // @note: @es3n1n: insert padding if needed
                    //
                    const auto expected_offset = state.last_field_offset + state.last_field_size;
                    if (state.last_field_offset && state.last_field_size && expected_offset < field->m_single_inheritance_offset &&
                        !state.assembling_bitfield) {

                        builder.access_modifier("private")
                            .struct_padding(expected_offset, field->m_single_inheritance_offset - expected_offset, false, true)
                            .reset_tabs_count()
                            .comment(std::format("{:#x}", expected_offset))
                            .restore_tabs_count()
                            .access_modifier("public");
                    }

                    // @note: @es3n1n: begin union if we're assembling bitfields
                    //
                    if (!state.assembling_bitfield && var_info.is_bitfield()) {
                        builder.begin_bitfield_block();
                        state.assembling_bitfield = true;
                    }

                    // @note: @es3n1n: if we are done with bitfields we should insert a pad and finish union
                    //
                    if (state.assembling_bitfield && !var_info.is_bitfield()) {
                        const auto expected_union_size_bytes = field->m_single_inheritance_offset - state.last_field_offset;
                        const auto expected_union_size_bits = expected_union_size_bytes * 8;

                        const auto actual_union_size_bits = state.total_bits_count_in_union;

                        if (expected_union_size_bits < state.total_bits_count_in_union)
                            throw std::runtime_error(
                                std::format("Unexpected union size: {}. Expected: {}", state.total_bits_count_in_union, expected_union_size_bits));

                        if (expected_union_size_bits > state.total_bits_count_in_union)
                            builder.struct_padding(std::nullopt, 0, true, false, expected_union_size_bits - actual_union_size_bits);

                        state.last_field_offset += expected_union_size_bytes;
                        state.last_field_size = expected_union_size_bytes;

                        builder.end_bitfield_block(false)
                            .reset_tabs_count()
                            .comment(std::format("{:d} bits", expected_union_size_bits))
                            .restore_tabs_count();

                        state.total_bits_count_in_union = 0ull;
                        state.assembling_bitfield = false;
                    }

                    // @note: @es3n1n: dump metadata
                    //
                    for (auto j = 0; j < field->m_metadata_size; j++) {
                        auto field_metadata = field->m_metadata[j];

                        if (auto data = get_metadata_type(field_metadata); data.empty())
                            builder.comment(field_metadata.m_name);
                        else
                            builder.comment(std::format("{} \"{}\"", field_metadata.m_name, data));
                    }

                    // @note: @es3n1n: update state
                    //
                    if (field->m_single_inheritance_offset && field_size) {
                        state.last_field_offset = field->m_single_inheritance_offset;
                        state.last_field_size = static_cast<std::size_t>(field_size);
                    }
                    if (var_info.is_bitfield())
                        state.total_bits_count_in_union += var_info.m_bitfield_size;

                    // @note: @es3n1n: push prop
                    //
                    builder.prop(var_info.m_type, var_info.formatted_name(), false);
                    if (!var_info.is_bitfield())
                        builder.reset_tabs_count().comment(std::format("{:#x}", field->m_single_inheritance_offset), false).restore_tabs_count();
                    builder.next_line();
                }

                // @note: @es3n1n: if struct ends with union we should end union before ending the class
                //
                if (state.assembling_bitfield) {
                    const auto actual_union_size_bits = state.total_bits_count_in_union;

                    // @note: @es3n1n: apply 8 bytes align
                    //
                    const auto expected_union_size_bits = actual_union_size_bits + (actual_union_size_bits % 8);

                    if (expected_union_size_bits > actual_union_size_bits)
                        builder.struct_padding(std::nullopt, 0, true, false, expected_union_size_bits - actual_union_size_bits);

                    builder.end_bitfield_block(false).reset_tabs_count().comment(std::format("{:d} bits", expected_union_size_bits)).restore_tabs_count();

                    state.total_bits_count_in_union = 0;
                    state.assembling_bitfield = false;
                }

                // @note: @es3n1n: dump static fields
                //
                if (class_info->m_static_size) {
                    if (class_info->m_align)
                        builder.next_line();
                    builder.comment("Static fields:");
                }
                for (auto s = 0; s < class_info->m_static_size; s++) {
                    auto static_field = &class_info->m_static_fields[s];

                    auto [type, mod] = get_type(static_field->m_type);
                    const auto var_info = field_parser::parse(type, static_field->name, mod);
                    builder.static_field_getter(var_info.m_type, var_info.m_name, current->GetScopeName().data(), class_info->m_name, s);
                }

                if (!class_info->m_align && !class_info->m_static_size)
                    builder.comment("No members available");

                builder.end_block();
            }
        }
    } // namespace

    void GenerateTypeScopeSdk(CSchemaSystemTypeScope* current) {
        // @note: @es3n1n: getting current scope name & formatting it
        //
        constexpr std::string_view dll_extension = ".dll";
        auto scope_name = current->GetScopeName();
        if (ends_with(scope_name.data(), dll_extension.data()))
            scope_name.remove_suffix(dll_extension.size());

        // @note: @es3n1n: print debug info
        //
        std::cout << std::format("{}: Assembling {}", __FUNCTION__, scope_name) << std::endl;

        // @note: @es3n1n: build file path
        //
        if (!std::filesystem::exists(kOutDirName))
            std::filesystem::create_directories(kOutDirName);
        const std::string out_file_path = std::format("{}\\{}.hpp", kOutDirName, scope_name);

        // @note: @es3n1n: init codegen
        //
        auto builder = codegen::get();
        builder.pragma("once");

        // @note: @es3n1n: include files
        //
        for (auto&& include_path : kIncludePaths)
            builder.include(include_path.data());

        // @note: @es3n1n: get stuff from schema that we'll use later
        //
        const auto current_classes = current->GetClasses();
        const auto current_enums = current->GetEnums();

        // @note: @es3n1n: print banner
        //
        builder.next_line()
            .comment("/////////////////////////////////////////////////////////////")
            .comment(std::format("Binary: {}", current->GetScopeName()))
            .comment(std::format("Classes count: {}", current_classes.Count()))
            .comment(std::format("Enums count: {}", current_enums.Count()))
            .comment(kCreatedBySource2genMessage.data())
            .comment("/////////////////////////////////////////////////////////////")
            .next_line();

        // @note: @es3n1n: assemble props
        //
        AssembleEnums(builder, current_enums);
        AssembleClasses(current, builder, current_classes);

        // @note: @es3n1n: write generated data to output file
        //
        std::ofstream f(out_file_path, std::ios::out);
        f << builder.str();
        f.close();
    }
} // namespace sdk
