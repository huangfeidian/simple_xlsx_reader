﻿#pragma once
#include "xlsx_worksheet.h"
#include <memory>
#include "xlsx_archive.h"
#include <optional>
namespace spiritsaway::xlsx_reader
{
	template <typename worksheet_t> 
	class workbook
	{
	public:
		std::vector<std::unique_ptr<worksheet_t>> _worksheets;
		std::vector<sheet_desc> sheet_relations; 
		
	private:
		std::vector<std::string> shared_string;
		std::unordered_map<std::string, std::uint32_t> shared_string_indexes;
		std::unordered_map<std::string_view, std::uint32_t> sheets_name_map;
		std::uint32_t get_index_for_string(const std::string& in_str)
		{
			auto iter = shared_string_indexes.find(in_str);
			if (iter != shared_string_indexes.end())
			{
				return iter->second;
			}
			else
			{
				shared_string.push_back(in_str);
				shared_string_indexes[in_str] = shared_string.size() - 1;
				return shared_string.size() - 1;
			}
		}
		
	public:
		std::optional<std::uint32_t> get_sheet_index_by_name(std::string_view sheet_name) const
		{
			auto iter = sheets_name_map.find(sheet_name);
			if(iter == sheets_name_map.end())
			{
				return std::nullopt;
			}
			else
			{
				return iter->second;
			}
		}
		const worksheet_t& get_worksheet(std::uint32_t sheet_idx) const
		{
			return *(_worksheets[sheet_idx]);
		}
		std::uint32_t get_worksheet_num() const
		{
			return _worksheets.size();
		}
		std::string_view workbook_name;
		std::string_view get_workbook_name() const
		{
			return workbook_name;
		}
		std::string_view get_shared_string(std::uint32_t ss_idx) const
		{
			if (ss_idx >= shared_string.size())
			{
				return std::string_view();
			}
			else
			{
				return std::string_view(shared_string[ss_idx]);
			}
		}

		workbook(std::shared_ptr<archive> in_archive)
		{
			archive_content = in_archive;
		
			auto in_shared_string = in_archive->get_shared_string();
			for (int i = 0; i < in_shared_string.size(); i++)
			{
				auto cur_string_view = strip_blank(std::string_view(in_shared_string[i]));
				std::string cur_string(cur_string_view);
				shared_string.push_back(cur_string);
				shared_string_indexes[shared_string[i]] = i;
			}
			sheet_relations = in_archive->get_all_sheet_relation();
			// we should load all shared strings here incase any relocation of the shared_string vector
			// invalidate the string_views that ref the shared string
			for (int i = 0; i < sheet_relations.size(); i++)
			{
				get_cells_for_sheet(i + 1);
			}
			shared_string.shrink_to_fit();

			// from now the shared_string begins to function
			for(int i = 0; i < sheet_relations.size(); i++)
			{
				auto cur_worksheet = new worksheet_t(get_cells_for_sheet(i + 1), get<1>(sheet_relations[i]), get<0>(sheet_relations[i]), this);
				cur_worksheet->after_load_process();
				_worksheets.emplace_back(cur_worksheet);
				auto current_sheet_name = cur_worksheet->get_name();
				sheets_name_map[current_sheet_name] = i;
			}
			after_load_process();
		}
		
		friend std::ostream& operator<<(std::ostream& output_stream, const workbook& in_workbook)
		{
			output_stream<<"workbook name:"<<string(in_workbook.get_workbook_name())<<endl;
			for(const auto& one_worksheet: in_workbook._worksheets)
			{
				output_stream<<*one_worksheet<<endl;
			}
			return output_stream;
		}
		std::uint32_t memory_details() const
		{
			std::uint32_t result = 0;
			for (const auto& i : _worksheets)
			{
				if (i)
				{
					result += i->memory_details();
				}
			}
			std::cout << "_worksheets memory " << result << std::endl;
			std::uint32_t ss_size = 0;
			ss_size = shared_string.capacity() * sizeof(std::string);
			std::cout << "shared_strings memory " << ss_size << "with size " << shared_string.size() << std::endl;
			result += ss_size;
			std::cout << "workbook " << workbook_name << "memory " << result << std::endl;
			return result;
		}
	protected:
		std::shared_ptr<archive> archive_content;
		std::shared_ptr<tinyxml2::XMLDocument> get_sheet_xml(std::uint32_t sheet_idx) const
		{
			auto sheet_path = "xl/worksheets/sheet" + to_string(sheet_idx) + ".xml";
			return archive_content->get_xml_document(sheet_path);
		}
		
		void after_load_process()
		{
			cout<<"Workbook "<<workbook_name<<" total sheets "<<_worksheets.size()<<std::endl;
		}
		std::unordered_map<std::uint32_t, std::vector<cell>> all_cells;

		const std::vector<cell>& get_cells_for_sheet(std::uint32_t sheet_idx)
		{
			auto cache_iter = all_cells.find(sheet_idx);
			if(cache_iter != all_cells.end())
			{
				return cache_iter->second;
			}
			auto& result = all_cells[sheet_idx];
			auto sheet_doc = get_sheet_xml(sheet_idx);

			auto worksheet_node = sheet_doc->FirstChildElement("worksheet");
			auto sheet_data_node = worksheet_node->FirstChildElement("sheetData");
			auto row_node = sheet_data_node->FirstChildElement("row");
			while(row_node)
			{
				uint32_t row_index = stoi(row_node->Attribute("r"));
				auto cell_node = row_node->FirstChildElement("c");
				while(cell_node)
				{
					uint32_t col_idx = row_column_tuple_from_string(cell_node->Attribute("r")).second;
					auto v_node = cell_node->FirstChildElement("v");
					if (!v_node)
					{
						break;
					}

					std::string_view current_value = cell_node->FirstChildElement("v")->GetText();
					current_value = strip_blank(current_value);
					auto type_attr = cell_node->Attribute("t");
					std::uint32_t ss_idx = 0;
					if(type_attr && *type_attr == 's')
					{
						ss_idx = stoi(string(current_value));
					}
					else
					{
						ss_idx = get_index_for_string(std::string(current_value));
					}
					cell_node = cell_node->NextSiblingElement("c");
					result.emplace_back(row_index, col_idx, ss_idx);

				}
				row_node = row_node->NextSiblingElement("row");
			}
			return result;
		}
	};
}