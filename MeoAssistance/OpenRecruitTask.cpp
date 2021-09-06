#include "OpenRecruitTask.h"

#include <map>
#include <vector>
#include <algorithm>

#include "Configer.h"
#include "RecruitConfiger.h"
#include "AsstAux.h"

using namespace asst;

OpenRecruitTask::OpenRecruitTask(AsstCallback callback, void* callback_arg)
	: OcrAbstractTask(callback, callback_arg)
{
	m_task_type = TaskType::TaskTypeRecognition & TaskType::TaskTypeClick;
}

bool OpenRecruitTask::run()
{
	if (m_view_ptr == NULL
		|| m_identify_ptr == NULL)
	{
		m_callback(AsstMsg::PtrIsNull, json::value(), m_callback_arg);
		return false;
	}

	json::value task_start_json = json::object{
		{ "task_type",  "OpenRecruitTask" },
		{ "task_chain", m_task_chain},
	};
	m_callback(AsstMsg::TaskStart, task_start_json, m_callback_arg);

	/* Find all text */
	std::vector<TextArea> all_text_area = ocr_detect();

	/* Filter out all tags from all text */
	std::vector<TextArea> all_tags = text_match(
		all_text_area, RecruitConfiger::get_instance().m_all_tags, Configer::get_instance().m_recruit_ocr_replace);

	std::unordered_set<std::string> all_tags_name;
	std::vector<json::value> all_tags_json_vector;
	for (const TextArea& text_area : all_tags) {
		all_tags_name.emplace(text_area.text);
		all_tags_json_vector.emplace_back(Utf8ToGbk(text_area.text));
	}
	json::value all_tags_json;
	all_tags_json["tags"] = json::array(all_tags_json_vector);

	/* ����tags��������������������ʶ��©�ˣ� */
	if (all_tags.size() != RecruitConfiger::CorrectNumberOfTags) {
		all_tags_json["type"] = "OpenRecruit";
		m_callback(AsstMsg::OcrResultError, all_tags_json, m_callback_arg);
		return false;
	}
	
	m_callback(AsstMsg::RecruitTagsDetected, all_tags_json, m_callback_arg);

	/* ������ļʱ��9Сʱ�������������*/
	if (m_set_time) {
		json::value settime_json;
		settime_json["task"] = "RecruitTime";
		// ֻ��tag�������˲����ߵ��������һ���ǶԵģ������Ҳ���ʱ�����þ�˵��ʱ���Ѿ��ֶ��޸Ĺ��ˣ�����������
		settime_json["retry_times"] = 0;
		settime_json["task_chain"] = m_task_chain;
		m_callback(AsstMsg::AppendProcessTask, settime_json, m_callback_arg);
	}

	/* ���һ�Ǹ�Ա�Ķ���ص���Ϣ */
	static const std::string SupportMachine_GBK = "֧Ԯ��е";
	static const std::string SupportMachine = GbkToUtf8(SupportMachine_GBK);
	if (std::find(all_tags_name.cbegin(), all_tags_name.cend(), SupportMachine) != all_tags_name.cend()) {
		json::value special_tag_json;
		special_tag_json["tag"] = SupportMachine_GBK;
		m_callback(AsstMsg::RecruitSpecialTag, special_tag_json, m_callback_arg);
	}

	// ʶ�𵽵�5��Tags��ȫ�������
	std::vector<std::vector<std::string>> all_combs;
	int len = all_tags.size();
	int count = std::pow(2, len);
	for (int i = 0; i < count; ++i) {
		std::vector<std::string> temp;
		for (int j = 0, mask = 1; j < len; ++j) {
			if ((i & mask) != 0) {	// What the fuck???
				temp.emplace_back(all_tags.at(j).text);
			}
			mask = mask * 2;
		}
		// ��Ϸ�����ѡ��3��tag
		if (!temp.empty() && temp.size() <= 3) {
			all_combs.emplace_back(std::move(temp));
		}
	}

	// key: tags comb, value: ��Ա���
	// ���� key: { "�ѻ�"��"Ⱥ��" }��value: OperRecruitCombs.opers{ "����", "��ѩ", "�ձ�" }
	std::map<std::vector<std::string>, OperRecruitCombs> result_map;
	for (const std::vector<std::string>& comb : all_combs) {
		for (const OperRecruitInfo& cur_oper : RecruitConfiger::get_instance().m_all_opers) {
			int matched_count = 0;
			for (const std::string& tag : comb) {
				if (cur_oper.tags.find(tag) != cur_oper.tags.cend()) {
					++matched_count;
				}
				else {
					break;
				}
			}

			// ����tags comb�е�ÿһ��tag�������Ա���У�����ø�Ա�������tags comb
			if (matched_count != comb.size()) {
				continue;
			}

			if (cur_oper.level == 6) {
				// ����tag������ǿ�󶨣����û�и���tag����ʹ����tagƥ������Ҳ�����ܳ�����
				static const std::string SeniorOper = GbkToUtf8("�߼������Ա");
				if (std::find(comb.cbegin(), comb.cend(), SeniorOper) == comb.cend()) {
					continue;
				}
			}

			OperRecruitCombs& oper_combs = result_map[comb];
			oper_combs.opers.emplace_back(cur_oper);

			if (cur_oper.level == 1 || cur_oper.level == 2) {
				if (oper_combs.min_level == 0) oper_combs.min_level = cur_oper.level;
				if (oper_combs.max_level == 0) oper_combs.max_level = cur_oper.level;
				// һ�ǡ����Ǹ�Ա��������͵ȼ�����Ϊ����9Сʱ֮�󲻿��ܳ�1��2��
				continue;
			}
			if (oper_combs.min_level == 0 || oper_combs.min_level > cur_oper.level) {
				oper_combs.min_level = cur_oper.level;
			}
			if (oper_combs.max_level == 0 || oper_combs.max_level < cur_oper.level) {
				oper_combs.max_level = cur_oper.level;
			}
			oper_combs.avg_level += cur_oper.level;
		}
		if (result_map.find(comb) != result_map.cend()) {
			OperRecruitCombs& oper_combs = result_map[comb];
			oper_combs.avg_level /= oper_combs.opers.size();
		}
	}

	// mapû����ֵ����ת��vector������
	std::vector<std::pair<std::vector<std::string>, OperRecruitCombs>> result_vector;
	for (auto&& pair : result_map) {
		result_vector.emplace_back(std::move(pair));
	}
	std::sort(result_vector.begin(), result_vector.end(), [](const auto& lhs, const auto& rhs)
		->bool {
			// ��С�ȼ���ģ���ǰ��
			if (lhs.second.min_level != rhs.second.min_level) {
				return lhs.second.min_level > rhs.second.min_level;
			}
			// ���ȼ���ģ���ǰ��
			else if (lhs.second.max_level != rhs.second.max_level) {
				return lhs.second.max_level > rhs.second.max_level;
			}
			// ƽ���ȼ��ߵģ���ǰ��
			else if (std::fabs(lhs.second.avg_level - rhs.second.avg_level) > DoubleDiff) {
				return lhs.second.avg_level > rhs.second.avg_level;
			}
			// Tag�����ٵģ���ǰ��
			else {
				return lhs.first.size() < rhs.first.size();
			}
		});

	/* ����ʶ���� */
	std::vector<json::value> result_json_vector;
	for (const auto& [tags_comb, oper_comb] : result_vector) {
		json::value comb_json;

		std::vector<json::value> tags_json_vector;
		for (const std::string& tag : tags_comb) {
			tags_json_vector.emplace_back(Utf8ToGbk(tag));
		}
		comb_json["tags"] = json::array(std::move(tags_json_vector));

		std::vector<json::value> opers_json_vector;
		for (const OperRecruitInfo& oper_info : oper_comb.opers) {
			json::value oper_json;
			oper_json["name"] = Utf8ToGbk(oper_info.name);
			oper_json["level"] = oper_info.level;
			opers_json_vector.emplace_back(std::move(oper_json));
		}
		comb_json["opers"] = json::array(std::move(opers_json_vector));
		comb_json["tag_level"] = oper_comb.min_level;
		result_json_vector.emplace_back(std::move(comb_json));
	}
	json::value results_json;
	results_json["result"] = json::array(std::move(result_json_vector));
	m_callback(AsstMsg::RecruitResult, results_json, m_callback_arg);

	/* ������Ž��tags�����ӵ������ */
	if (!m_required_level.empty() && !result_vector.empty()) {
		if (std::find(m_required_level.cbegin(), m_required_level.cend(), result_vector[0].second.min_level)
			== m_required_level.cend()) {
			return true;
		}
		const std::vector<std::string>& final_tags_name = result_vector[0].first;

		json::value task_json;
		task_json["type"] = "ClickTask";
		for (const TextArea& text_area : all_tags) {
			if (std::find(final_tags_name.cbegin(), final_tags_name.cend(), text_area.text) != final_tags_name.cend()) {
				task_json["rect"] = json::array({ text_area.rect.x, text_area.rect.y, text_area.rect.width, text_area.rect.height });
				task_json["retry_times"] = m_retry_times;
				task_json["task_chain"] = m_task_chain;
				m_callback(AsstMsg::AppendTask, task_json, m_callback_arg);
			}
		}
	}

	return true;
}

void OpenRecruitTask::set_param(std::vector<int> required_level, bool set_time)
{
	m_required_level = std::move(required_level);
	m_set_time = set_time;
}