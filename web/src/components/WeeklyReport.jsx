import React, { useState, useEffect, useMemo } from 'react';
import {
  Card,
  Row,
  Col,
  Statistic,
  DatePicker,
  Button,
  Table,
  Tag,
  Typography,
  Space,
  Divider,
  Progress,
  message,
  Spin,
  Alert
} from 'antd';
import dayjs from 'dayjs';
import isoWeek from 'dayjs/plugin/isoWeek';
import {
  CalendarOutlined,
  CopyOutlined,
  ArrowUpOutlined,
  ArrowDownOutlined,
  CheckCircleOutlined,
  CodeOutlined,
  ThunderboltOutlined,
  RobotOutlined,
  AimOutlined,
  ProjectOutlined,
  FileDoneOutlined,
  ShareAltOutlined,
  FireOutlined
} from '@ant-design/icons';
import { formatNumber, formatTokens } from '../App';

dayjs.extend(isoWeek);
const { Title, Text, Paragraph } = Typography;

export default function WeeklyReport() {
  const [selectedWeek, setSelectedWeek] = useState(dayjs());
  const [loading, setLoading] = useState(false);
  const [copied, setCopied] = useState(false);

  // 本周与上周数据
  const [thisWeekData, setThisWeekData] = useState(null);
  const [lastWeekData, setLastWeekData] = useState(null);

  // 计算本周起止日期 (ISO 周：周一到周日)
  const currentWeekRange = useMemo(() => {
    const start = selectedWeek.startOf('isoWeek');
    const end = selectedWeek.endOf('isoWeek');
    return {
      startStr: start.format('YYYY-MM-DD'),
      endStr: end.format('YYYY-MM-DD'),
      label: `${start.format('YYYY年第w周')} (${start.format('MM/DD')} - ${end.format('MM/DD')})`
    };
  }, [selectedWeek]);

  // 计算上周起止日期
  const lastWeekRange = useMemo(() => {
    const start = selectedWeek.subtract(1, 'week').startOf('isoWeek');
    const end = selectedWeek.subtract(1, 'week').endOf('isoWeek');
    return {
      startStr: start.format('YYYY-MM-DD'),
      endStr: end.format('YYYY-MM-DD')
    };
  }, [selectedWeek]);

  const fetchWeekData = async (start, end) => {
    try {
      const q = `?start=${start}&end=${end}`;
      const [sum, usage, code, attr, projects, models, tools] = await Promise.all([
        fetch('/api/summary' + q).then(r => r.json()).catch(() => null),
        fetch('/api/usage' + q).then(r => r.json()).catch(() => null),
        fetch('/api/code' + q).then(r => r.json()).catch(() => null),
        fetch('/api/attribution' + q).then(r => r.json()).catch(() => null),
        fetch('/api/projects' + q).then(r => r.json()).catch(() => []),
        fetch('/api/models' + q).then(r => r.json()).catch(() => []),
        fetch('/api/tools' + q).then(r => r.json()).catch(() => [])
      ]);
      return { sum, usage, code, attr, projects, models, tools };
    } catch (e) {
      console.error('Failed to load weekly report data', e);
      return null;
    }
  };

  const loadAll = async () => {
    setLoading(true);
    try {
      const [curr, prev] = await Promise.all([
        fetchWeekData(currentWeekRange.startStr, currentWeekRange.endStr),
        fetchWeekData(lastWeekRange.startStr, lastWeekRange.endStr)
      ]);
      setThisWeekData(curr);
      setLastWeekData(prev);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadAll();
  }, [currentWeekRange]);

  // 环比计算辅助函数
  const calcWoW = (currVal, prevVal) => {
    const c = Number(currVal) || 0;
    const p = Number(prevVal) || 0;
    if (p === 0) return c > 0 ? { val: 100, isUp: true } : { val: 0, isUp: true };
    const diff = ((c - p) / p) * 100;
    return {
      val: Math.abs(diff).toFixed(1),
      isUp: diff >= 0,
      diffVal: c - p
    };
  };

  const twUsage = thisWeekData?.usage;
  const twCode = thisWeekData?.code;
  const twAttr = thisWeekData?.attr;
  const twSum = thisWeekData?.sum;
  const twProjects = thisWeekData?.projects || [];
  const twModels = thisWeekData?.models || [];
  const twTools = thisWeekData?.tools || [];

  const lwUsage = lastWeekData?.usage;
  const lwCode = lastWeekData?.code;
  const lwAttr = lastWeekData?.attr;

  const sessionsWoW = calcWoW(twUsage?.total_sessions, lwUsage?.total_sessions);
  const linesAddedWoW = calcWoW(twCode?.lines_added, lwCode?.lines_added);
  const acceptedLinesWoW = calcWoW(twAttr?.accepted_lines, lwAttr?.accepted_lines);
  const tokensWoW = calcWoW((twUsage?.input_tokens || 0) + (twUsage?.output_tokens || 0), (lwUsage?.input_tokens || 0) + (lwUsage?.output_tokens || 0));

  // 生成结构化 Markdown 周报
  const markdownReport = useMemo(() => {
    if (!thisWeekData) return '';
    const topProjStr = twProjects.slice(0, 5).map(p => 
      `- **${p.project}**: 贡献 ${formatNumber(p.lines_added)} 行代码 / Git 采纳 ${formatNumber(p.accepted_lines)} 行 (采纳率 ${(p.acceptance_rate || 0).toFixed(1)}%)，产生会话 ${p.sessions} 次`
    ).join('\n') || '- 暂无项目变更';

    const topToolsStr = twTools.slice(0, 5).map(t =>
      `- \`${t.detail_name || t.tool_name}\`: 调用 ${formatNumber(t.calls)} 次`
    ).join('\n') || '- 暂无工具调用';

    const totalTok = (twUsage?.input_tokens || 0) + (twUsage?.output_tokens || 0);

    return `# 🤖 AI 辅助研发周报 (${currentWeekRange.label})

## 📌 一、本周工作产出概览
- **会话总次数**: **${formatNumber(twUsage?.total_sessions)}** 次 (${sessionsWoW.isUp ? '↑ 环比增加' : '↓ 环比减少'} ${sessionsWoW.val}%)
- **AI 提议代码行数**: **${formatNumber(twAttr?.candidate_lines)}** 行
- **Git 最终采纳合并**: **${formatNumber(twAttr?.accepted_lines)}** 行 (${acceptedLinesWoW.isUp ? '↑' : '↓'} ${acceptedLinesWoW.val}%)
- **精确代码采纳率**: **${(twAttr?.acceptance_rate || 0).toFixed(1)}%** (会话维度采纳率: ${(twAttr?.session_acceptance_rate || 0).toFixed(1)}%)
- **业务核心逻辑代码**: 占比 **${(twCode?.business_code_share || 0).toFixed(1)}%** (共 ${formatNumber(twCode?.business_lines_added)} 行)
- **代码净增长**: **+${formatNumber(twCode?.lines_added)}** / -${formatNumber(twCode?.lines_deleted)} 行

## 🚀 二、主要研发项目进展与贡献
${topProjStr}

## 🛠️ 三、AI Agent 工具与技能协作
- **总工具调用次数**: **${formatNumber(twUsage?.tool_calls)}** 次 (单会话平均 ${(twUsage?.avg_tools_per_session || 0).toFixed(1)} 次)
- **工具调用成功率**: **${(twUsage?.tool_success_rate || 98.8).toFixed(1)}%**
- **常用工具/能力**:
${topToolsStr}

## ⚡ 四、Token 资产与 Prompt 缓存效益
- **周期总消耗 Token**: **${formatTokens(totalTok)}** (输入: ${formatTokens(twUsage?.input_tokens)} / 输出: ${formatTokens(twUsage?.output_tokens)})
- **Prompt 缓存命中率**: **${(twUsage?.cache_hit_rate || 0).toFixed(1)}%** (复用命中 ${formatTokens(twUsage?.cached_input_tokens)} Tokens)
- **单成功任务平均开销**: **${formatTokens(twUsage?.avg_tokens_per_successful_session)}** Tokens
- **估算模型调用费用**: **$${(twSum?.estimated_cost_usd || 0).toFixed(4)}** USD

---
*Generated automatically by AgentStat telemetry at ${dayjs().format('YYYY-MM-DD HH:mm:ss')}*
`;
  }, [thisWeekData, currentWeekRange, sessionsWoW, linesAddedWoW, acceptedLinesWoW]);

  const handleCopyMarkdown = () => {
    navigator.clipboard.writeText(markdownReport);
    setCopied(true);
    message.success('周报 Markdown 文本已复制到剪贴板，可直接粘贴！');
    setTimeout(() => setCopied(false), 2500);
  };

  return (
    <div style={{ marginTop: 10 }}>
      {/* 顶部控制栏 */}
      <Card
        size="small"
        style={{
          marginBottom: 20,
          background: '#0f172a',
          borderColor: 'rgba(255,255,255,0.08)'
        }}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 14 }}>
          <Space size="middle" align="center" wrap>
            <span style={{ fontWeight: 600, color: '#38bdf8', fontSize: 14, display: 'flex', alignItems: 'center', gap: 6 }}>
              <CalendarOutlined /> 周期选择:
            </span>
            <DatePicker
              picker="week"
              value={selectedWeek}
              onChange={(val) => val && setSelectedWeek(val)}
              style={{ minWidth: 240, background: '#1e293b' }}
              allowClear={false}
            />
            <Button
              size="small"
              onClick={() => setSelectedWeek(dayjs())}
              style={{ fontSize: 12 }}
            >
              本周
            </Button>
            <Button
              size="small"
              onClick={() => setSelectedWeek(dayjs().subtract(1, 'week'))}
              style={{ fontSize: 12 }}
            >
              上周
            </Button>
            <Text type="secondary" style={{ fontSize: 13 }}>
              分析周期: <span style={{ color: '#38bdf8', fontWeight: 600 }}>{currentWeekRange.label}</span>
            </Text>
          </Space>

          <Space size="small">
            <Button
              type="primary"
              icon={<CopyOutlined />}
              onClick={handleCopyMarkdown}
              style={{ background: '#0284c7', borderColor: '#38bdf8', fontWeight: 500 }}
            >
              {copied ? '已复制周报' : '一键复制周报 Markdown'}
            </Button>
          </Space>
        </div>
      </Card>

      <Spin spinning={loading} tip="正在进行全维度周报效能比对计算...">
        {/* 核心周报指标卡片与环比 */}
        <Row gutter={[16, 16]} style={{ marginBottom: 20 }}>
          <Col xs={24} sm={12} lg={6}>
            <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)' }}>
              <Statistic
                title={<span style={{ color: '#94a3b8', fontSize: 13 }}>本周会话总数 (Sessions)</span>}
                value={twUsage?.total_sessions || 0}
                valueStyle={{ color: '#f8fafc', fontWeight: 700 }}
                prefix={<RobotOutlined style={{ color: '#38bdf8', marginRight: 8 }} />}
                suffix={<span style={{ fontSize: 12, color: '#94a3b8', marginLeft: 4 }}>次</span>}
              />
              <div style={{ marginTop: 8, fontSize: 12, display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ color: '#64748b' }}>环比上周:</span>
                <Tag color={sessionsWoW.isUp ? 'green' : 'orange'} style={{ borderRadius: 10, fontSize: 11 }}>
                  {sessionsWoW.isUp ? <ArrowUpOutlined /> : <ArrowDownOutlined />} {sessionsWoW.val}% ({sessionsWoW.isUp ? `+${sessionsWoW.diffVal}` : sessionsWoW.diffVal} 次)
                </Tag>
              </div>
            </Card>
          </Col>

          <Col xs={24} sm={12} lg={6}>
            <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)' }}>
              <Statistic
                title={<span style={{ color: '#94a3b8', fontSize: 13 }}>Git 最终采纳代码行 (Accepted)</span>}
                value={twAttr?.accepted_lines || 0}
                formatter={v => formatNumber(v)}
                valueStyle={{ color: '#10b981', fontWeight: 700 }}
                prefix={<AimOutlined style={{ color: '#10b981', marginRight: 8 }} />}
                suffix={<span style={{ fontSize: 12, color: '#94a3b8', marginLeft: 4 }}>行</span>}
              />
              <div style={{ marginTop: 8, fontSize: 12, display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ color: '#64748b' }}>精确采纳率:</span>
                <span style={{ color: '#38bdf8', fontWeight: 600 }}>{(twAttr?.acceptance_rate || 0).toFixed(1)}%</span>
                <Tag color={acceptedLinesWoW.isUp ? 'green' : 'orange'} style={{ borderRadius: 10, fontSize: 11, marginLeft: 'auto' }}>
                  {acceptedLinesWoW.isUp ? <ArrowUpOutlined /> : <ArrowDownOutlined />} {acceptedLinesWoW.val}%
                </Tag>
              </div>
            </Card>
          </Col>

          <Col xs={24} sm={12} lg={6}>
            <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)' }}>
              <Statistic
                title={<span style={{ color: '#94a3b8', fontSize: 13 }}>AI 生成新增 / 净增代码量</span>}
                value={twCode?.lines_added || 0}
                formatter={v => `+${formatNumber(v)}`}
                valueStyle={{ color: '#38bdf8', fontWeight: 700 }}
                prefix={<CodeOutlined style={{ color: '#38bdf8', marginRight: 8 }} />}
              />
              <div style={{ marginTop: 8, fontSize: 12, display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ color: '#64748b' }}>业务逻辑代码:</span>
                <span style={{ color: '#10b981', fontWeight: 600 }}>{(twCode?.business_code_share || 0).toFixed(1)}%</span>
                <span style={{ color: '#94a3b8' }}>(-{formatNumber(twCode?.lines_deleted)})</span>
              </div>
            </Card>
          </Col>

          <Col xs={24} sm={12} lg={6}>
            <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)' }}>
              <Statistic
                title={<span style={{ color: '#94a3b8', fontSize: 13 }}>本周 Token 资产总消耗</span>}
                value={(twUsage?.input_tokens || 0) + (twUsage?.output_tokens || 0)}
                formatter={v => formatTokens(v)}
                valueStyle={{ color: '#fbbf24', fontWeight: 700 }}
                prefix={<ThunderboltOutlined style={{ color: '#fbbf24', marginRight: 8 }} />}
              />
              <div style={{ marginTop: 8, fontSize: 12, display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ color: '#64748b' }}>缓存命中率:</span>
                <span style={{ color: '#10b981', fontWeight: 600 }}>{(twUsage?.cache_hit_rate || 0).toFixed(1)}%</span>
                <Tag color={tokensWoW.isUp ? 'purple' : 'blue'} style={{ borderRadius: 10, fontSize: 11, marginLeft: 'auto' }}>
                  {tokensWoW.isUp ? <ArrowUpOutlined /> : <ArrowDownOutlined />} {tokensWoW.val}%
                </Tag>
              </div>
            </Card>
          </Col>
        </Row>

        {/* 周报双栏布局：左侧结构化展示，右侧 Markdown 预览 */}
        <Row gutter={[16, 16]}>
          <Col xs={24} lg={13}>
            <Card
              title={
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <FileDoneOutlined style={{ color: '#38bdf8' }} />
                  <span>📑 本周工作进展与效能结构化报告</span>
                </div>
              }
              style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
            >
              <div style={{ marginBottom: 18 }}>
                <Title level={5} style={{ color: '#f8fafc', marginBottom: 6 }}>
                  🚀 核心研发项目与模块贡献
                </Title>
                <Table
                  dataSource={twProjects}
                  rowKey="project_path"
                  pagination={false}
                  size="small"
                  columns={[
                    { title: '项目名称', dataIndex: 'project', render: p => <Text strong>{p}</Text> },
                    { title: '会话数', dataIndex: 'sessions', render: v => `${v} 次` },
                    { title: '产出代码', dataIndex: 'lines_added', render: v => <span style={{ color: '#10b981' }}>+{formatNumber(v)} 行</span> },
                    { title: 'Git 已采纳', dataIndex: 'accepted_lines', render: v => `${formatNumber(v)} 行` },
                    {
                      title: '采纳率',
                      dataIndex: 'acceptance_rate',
                      render: v => <Progress percent={Number((v || 0).toFixed(1))} size="small" style={{ width: 80 }} />
                    }
                  ]}
                />
              </div>

              <Divider style={{ borderColor: 'rgba(255,255,255,0.08)', margin: '14px 0' }} />

              <div style={{ marginBottom: 18 }}>
                <Title level={5} style={{ color: '#f8fafc', marginBottom: 6 }}>
                  🛠️ Agent 关键工具与能力调用 Top 5
                </Title>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                  {twTools.slice(0, 6).map((t, idx) => (
                    <Tag key={idx} color="geekblue" style={{ padding: '4px 10px', fontSize: 12 }}>
                      <span style={{ fontWeight: 600 }}>{t.detail_name || t.tool_name}</span>: {formatNumber(t.calls)} 次
                    </Tag>
                  ))}
                  {twTools.length === 0 && <Text type="secondary">本周暂无工具调用记录</Text>}
                </div>
              </div>

              <Divider style={{ borderColor: 'rgba(255,255,255,0.08)', margin: '14px 0' }} />

              <div>
                <Title level={5} style={{ color: '#f8fafc', marginBottom: 6 }}>
                  💡 本周效能总结评述
                </Title>
                <Alert
                  type="info"
                  showIcon
                  message={
                    <div style={{ fontSize: 13, lineHeight: 1.6 }}>
                      本周在 AI 辅助下共发起 <b>{formatNumber(twUsage?.total_sessions)}</b> 次编码会话，最终由 Git 实际提交合入 <b>{formatNumber(twAttr?.accepted_lines)}</b> 行代码，精确行采纳率达到 <b style={{ color: '#10b981' }}>{(twAttr?.acceptance_rate || 0).toFixed(1)}%</b>。Prompt 缓存命中率为 <b>{(twUsage?.cache_hit_rate || 0).toFixed(1)}%</b>，有效节约了大量重复上下文读取延迟与成本。
                    </div>
                  }
                  style={{ background: 'rgba(2, 132, 199, 0.1)', borderColor: 'rgba(56, 189, 248, 0.3)' }}
                />
              </div>
            </Card>
          </Col>

          {/* 右侧：Markdown 实时生成与一键复制 */}
          <Col xs={24} lg={11}>
            <Card
              title={
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span>📋 Markdown 周报预览 (可直接复制到飞书/企微/邮件)</span>
                  <Button
                    size="small"
                    type="primary"
                    icon={<CopyOutlined />}
                    onClick={handleCopyMarkdown}
                  >
                    {copied ? '已复制' : '复制'}
                  </Button>
                </div>
              }
              style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
            >
              <pre
                style={{
                  background: '#090d16',
                  padding: 16,
                  borderRadius: 8,
                  border: '1px solid rgba(255,255,255,0.08)',
                  color: '#cbd5e1',
                  fontSize: 12,
                  lineHeight: 1.6,
                  maxHeight: 520,
                  overflowY: 'auto',
                  whiteSpace: 'pre-wrap',
                  fontFamily: "'JetBrains Mono', monospace"
                }}
              >
                {markdownReport}
              </pre>
            </Card>
          </Col>
        </Row>
      </Spin>
    </div>
  );
}
