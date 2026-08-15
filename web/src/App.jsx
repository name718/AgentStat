import React, { useState, useEffect, useMemo } from 'react';
import {
  ConfigProvider,
  theme,
  Layout,
  Card,
  Row,
  Col,
  Statistic,
  DatePicker,
  Segmented,
  Tabs,
  Button,
  Table,
  Tag,
  Tooltip,
  Badge,
  Space,
  Input,
  Select,
  Spin,
  Drawer,
  Typography,
  Divider,
  Progress,
  message
} from 'antd';
import zhCN from 'antd/locale/zh_CN';
import dayjs from 'dayjs';
import 'dayjs/locale/zh-cn';
import {
  RobotOutlined,
  ThunderboltOutlined,
  AimOutlined,
  CodeOutlined,
  ToolOutlined,
  DatabaseOutlined,
  DollarCircleOutlined,
  SyncOutlined,
  DownloadOutlined,
  InfoCircleOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined,
  SearchOutlined,
  FileTextOutlined,
  BarChartOutlined,
  PieChartOutlined,
  FolderOutlined,
  ClockCircleOutlined,
  CalendarOutlined,
  HistoryOutlined,
  FireOutlined
} from '@ant-design/icons';
import EChartComponent from './components/EChartComponent';
import TranscriptModal from './components/TranscriptModal';

dayjs.locale('zh-cn');
const { Header, Content } = Layout;
const { Text, Title, Paragraph } = Typography;
const { RangePicker } = DatePicker;

export function formatNumber(num) {
  if (num === null || num === undefined) return '0';
  return Number(num).toLocaleString('zh-CN');
}

export function formatTokens(tokens) {
  if (!tokens) return '0';
  const n = Number(tokens);
  if (n >= 1e9) return (n / 1e9).toFixed(2) + ' B';
  if (n >= 1e6) return (n / 1e6).toFixed(2) + ' M';
  if (n >= 1e3) return (n / 1e3).toFixed(1) + ' K';
  return n.toString();
}

function MetricTip({ title, description, formula }) {
  return (
    <Tooltip
      placement="top"
      color="#0f172a"
      title={
        <div style={{ maxWidth: 300, padding: '4px 2px' }}>
          <div style={{ fontWeight: 600, color: '#38bdf8', marginBottom: 4, fontSize: 13 }}>
            {title}
          </div>
          <div style={{ color: '#cbd5e1', fontSize: 12, lineHeight: 1.5 }}>
            {description}
          </div>
          {formula && (
            <div style={{ marginTop: 6, paddingTop: 6, borderTop: '1px dashed rgba(255,255,255,0.15)', fontSize: 11 }}>
              <span style={{ color: '#94a3b8', marginRight: 4 }}>计算公式:</span>
              <code style={{ color: '#34d399', background: 'rgba(52,211,153,0.1)', padding: '2px 4px', borderRadius: 3 }}>
                {formula}
              </code>
            </div>
          )}
        </div>
      }
    >
      <InfoCircleOutlined style={{ color: '#64748b', cursor: 'pointer', marginLeft: 6, fontSize: 13 }} />
    </Tooltip>
  );
}

export default function App() {
  const [summary, setSummary] = useState(null);
  const [usage, setUsage] = useState(null);
  const [codeStats, setCodeStats] = useState(null);
  const [attribution, setAttribution] = useState(null);
  const [models, setModels] = useState([]);
  const [tools, setTools] = useState([]);
  const [projects, setProjects] = useState([]);
  const [mcp, setMcp] = useState([]);
  const [skills, setSkills] = useState([]);
  const [timeseries, setTimeseries] = useState([]);
  const [sessions, setSessions] = useState([]);

  const [period, setPeriod] = useState('day');
  const [activeTab, setActiveTab] = useState('efficiency');
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [loading, setLoading] = useState(false);

  // 默认最近一周 (7天)
  const defaultDates = useMemo(() => [dayjs().subtract(6, 'day').startOf('day'), dayjs().endOf('day')], []);
  const [dateRange, setDateRange] = useState(defaultDates);

  // 会话列表分页与搜索
  const [sessionSearch, setSessionSearch] = useState('');
  const [sessionAgentFilter, setSessionAgentFilter] = useState('all');
  const [sessionPage, setSessionPage] = useState(1);
  const [pageSize, setPageSize] = useState(10);

  // 会话流水详情 Drawer 状态
  const [selectedSessionId, setSelectedSessionId] = useState(null);

  // 模型定价配置 Drawer
  const [pricingDrawerOpen, setPricingDrawerOpen] = useState(false);
  const [selectedPricingModel, setSelectedPricingModel] = useState(null);
  const [pricingForm, setPricingForm] = useState({
    input_rate: 0,
    cache_read_rate: 0,
    cache_write_rate: 0,
    output_rate: 0
  });

  const rangePresets = [
    { label: '今天', value: [dayjs().startOf('day'), dayjs().endOf('day')] },
    { label: '近 7 天 (默认)', value: [dayjs().subtract(6, 'day').startOf('day'), dayjs().endOf('day')] },
    { label: '近 30 天', value: [dayjs().subtract(29, 'day').startOf('day'), dayjs().endOf('day')] },
    { label: '本月', value: [dayjs().startOf('month'), dayjs().endOf('month')] },
    { label: '全部时间', value: [dayjs('2020-01-01'), dayjs()] }
  ];

  const buildQuery = (extra = '') => {
    const params = new URLSearchParams();
    if (dateRange && dateRange[0] && dateRange[1]) {
      params.append('start', dateRange[0].format('YYYY-MM-DD'));
      params.append('end', dateRange[1].format('YYYY-MM-DD'));
    }
    if (extra) {
      const extraParams = new URLSearchParams(extra);
      extraParams.forEach((v, k) => params.append(k, v));
    }
    const qs = params.toString();
    return qs ? `?${qs}` : '';
  };

  const fetchAllData = async () => {
    setLoading(true);
    try {
      const [
        sumRes, useRes, codeRes, attrRes, modRes, toolRes, projRes, mcpRes, skillRes, timeRes, sessRes
      ] = await Promise.all([
        fetch('/api/summary' + buildQuery()).then(r => r.json()).catch(() => null),
        fetch('/api/usage' + buildQuery()).then(r => r.json()).catch(() => null),
        fetch('/api/code' + buildQuery()).then(r => r.json()).catch(() => null),
        fetch('/api/attribution' + buildQuery()).then(r => r.json()).catch(() => null),
        fetch('/api/models' + buildQuery()).then(r => r.json()).catch(() => []),
        fetch('/api/tools' + buildQuery()).then(r => r.json()).catch(() => []),
        fetch('/api/projects' + buildQuery()).then(r => r.json()).catch(() => []),
        fetch('/api/mcp' + buildQuery()).then(r => r.json()).catch(() => []),
        fetch('/api/skills' + buildQuery()).then(r => r.json()).catch(() => []),
        fetch(`/api/timeseries` + buildQuery(`period=${period}`)).then(r => r.json()).catch(() => ({ rows: [] })),
        fetch('/api/sessions' + buildQuery()).then(r => r.json()).catch(() => [])
      ]);

      if (sumRes) setSummary(sumRes);
      if (useRes) setUsage(useRes);
      if (codeRes) setCodeStats(codeRes);
      if (attrRes) setAttribution(attrRes);
      if (Array.isArray(modRes)) setModels(modRes);
      if (Array.isArray(toolRes)) setTools(toolRes);
      if (Array.isArray(projRes)) setProjects(projRes);
      if (Array.isArray(mcpRes)) setMcp(mcpRes);
      if (Array.isArray(skillRes)) setSkills(skillRes);
      if (timeRes && Array.isArray(timeRes.rows)) setTimeseries(timeRes.rows);
      if (Array.isArray(sessRes)) setSessions(sessRes);
    } catch (e) {
      console.error("加载数据异常", e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchAllData();
  }, [period, dateRange]);

  useEffect(() => {
    let timer = null;
    if (autoRefresh) {
      timer = setInterval(fetchAllData, 30000);
    }
    return () => {
      if (timer) clearInterval(timer);
    };
  }, [autoRefresh, period, dateRange]);

  const triggerSync = async () => {
    setSyncing(true);
    try {
      await fetch('/api/sync').then(r => r.json()).catch(() => null);
      message.success('本地日志全量同步完成');
      await fetchAllData();
    } catch (e) {
      message.error('同步失败: ' + e.message);
    } finally {
      setSyncing(false);
    }
  };

  const handleOpenPricing = (record) => {
    setSelectedPricingModel(record);
    setPricingForm({
      input_rate: record.input_rate || 0,
      cache_read_rate: record.cache_read_rate || 0,
      cache_write_rate: record.cache_write_rate || 0,
      output_rate: record.output_rate || 0
    });
    setPricingDrawerOpen(true);
  };

  const handleSavePricing = async () => {
    if (!selectedPricingModel) return;
    try {
      const res = await fetch('/api/pricing', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          source: selectedPricingModel.source,
          model: selectedPricingModel.model,
          ...pricingForm
        })
      }).then(r => r.json());
      if (res.status === 'ok') {
        message.success('模型定价费率保存成功');
        setPricingDrawerOpen(false);
        fetchAllData();
      } else {
        message.error('保存失败: ' + (res.error || '未知错误'));
      }
    } catch (e) {
      message.error('请求失败: ' + e.message);
    }
  };

  const exportCSV = () => {
    if (!sessions || sessions.length === 0) {
      message.warning("当前筛选条件下无会话数据可导出");
      return;
    }
    const headers = ["会话ID", "来源Agent", "工作目录", "开始时间", "使用模型", "输入Token", "输出Token", "工具调用数", "代码变更数"];
    const csvRows = [headers.join(",")];
    sessions.forEach(s => {
      const row = [
        `"${s.session_id || ''}"`,
        `"${s.source || ''}"`,
        `"${(s.cwd || '').replace(/"/g, '""')}"`,
        `"${s.started_at || ''}"`,
        `"${(s.models || '').replace(/"/g, '""')}"`,
        s.input_tokens || 0,
        s.output_tokens || 0,
        s.tool_calls || 0,
        s.code_changes || 0
      ];
      csvRows.push(row.join(","));
    });
    const blob = new Blob(["\ufeff" + csvRows.join("\n")], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.setAttribute("href", url);
    link.setAttribute("download", `agentstat_sessions_${dayjs().format('YYYYMMDD_HHmmss')}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    message.success("CSV 会话报表已成功导出");
  };

  // 会话过滤
  const filteredSessions = useMemo(() => {
    return sessions.filter(s => {
      if (sessionAgentFilter !== 'all' && s.source !== sessionAgentFilter) return false;
      if (!sessionSearch) return true;
      const q = sessionSearch.toLowerCase();
      return (
        (s.session_id && s.session_id.toLowerCase().includes(q)) ||
        (s.cwd && s.cwd.toLowerCase().includes(q)) ||
        (s.models && s.models.toLowerCase().includes(q))
      );
    });
  }, [sessions, sessionSearch, sessionAgentFilter]);

  // ECharts 1: 工作效率与代码产出趋势
  const efficiencyChartOption = useMemo(() => {
    const sorted = [...timeseries].reverse();
    const dates = sorted.map(d => d.period_start);
    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        formatter: (params) => {
          let res = `<div style="font-weight:600;margin-bottom:4px;">${params[0].name}</div>`;
          params.forEach(p => {
            res += `<div style="display:flex;justify-content:space-between;gap:16px;margin:2px 0;">
              <span>${p.marker} ${p.seriesName}:</span>
              <span style="font-weight:600;">${formatNumber(p.value)}</span>
            </div>`;
          });
          return res;
        }
      },
      legend: {
        data: ['新增代码行', '删除代码行', '会话频次 (次)', '工具调用数'],
        textStyle: { color: '#94a3b8' },
        top: 0
      },
      grid: { left: '3%', right: '4%', bottom: '8%', top: '15%', containLabel: true },
      xAxis: {
        type: 'category',
        data: dates,
        axisLine: { lineStyle: { color: '#334155' } },
        axisLabel: { color: '#94a3b8', fontSize: 11 }
      },
      yAxis: [
        {
          type: 'value',
          name: '代码行数',
          nameTextStyle: { color: '#94a3b8' },
          axisLine: { lineStyle: { color: '#334155' } },
          splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
          axisLabel: { color: '#94a3b8' }
        },
        {
          type: 'value',
          name: '频次 / 次数',
          nameTextStyle: { color: '#94a3b8' },
          axisLine: { lineStyle: { color: '#334155' } },
          splitLine: { show: false },
          axisLabel: { color: '#94a3b8' }
        }
      ],
      series: [
        {
          name: '新增代码行',
          type: 'bar',
          stack: 'lines',
          itemStyle: { color: '#10b981', borderRadius: [3, 3, 0, 0] },
          data: sorted.map(d => d.lines_added)
        },
        {
          name: '删除代码行',
          type: 'bar',
          stack: 'lines',
          itemStyle: { color: '#f43f5e', borderRadius: [0, 0, 3, 3] },
          data: sorted.map(d => -d.lines_deleted)
        },
        {
          name: '会话频次 (次)',
          type: 'line',
          yAxisIndex: 1,
          smooth: true,
          itemStyle: { color: '#38bdf8' },
          lineStyle: { width: 3 },
          data: sorted.map(d => d.sessions)
        },
        {
          name: '工具调用数',
          type: 'line',
          yAxisIndex: 1,
          smooth: true,
          itemStyle: { color: '#a855f7' },
          lineStyle: { width: 2, type: 'dashed' },
          data: sorted.map(d => d.tool_calls)
        }
      ]
    };
  }, [timeseries]);

  // ECharts 2: Token 消耗趋势
  const tokenChartOption = useMemo(() => {
    const sorted = [...timeseries].reverse();
    const dates = sorted.map(d => d.period_start);
    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        formatter: (params) => {
          let res = `<div style="font-weight:600;margin-bottom:4px;">${params[0].name}</div>`;
          let sum = 0;
          params.forEach(p => {
            sum += Number(p.value) || 0;
            res += `<div style="display:flex;justify-content:space-between;gap:16px;margin:2px 0;">
              <span>${p.marker} ${p.seriesName}:</span>
              <span style="font-weight:600;">${formatTokens(p.value)}</span>
            </div>`;
          });
          res += `<div style="border-top:1px solid rgba(255,255,255,0.1);margin-top:4px;padding-top:4px;display:flex;justify-content:space-between;">
            <span>总计 Token:</span>
            <span style="font-weight:700;color:#38bdf8;">${formatTokens(sum)}</span>
          </div>`;
          return res;
        }
      },
      legend: {
        data: ['缓存命中 Token (Cached Input)', '直读输入 Token (Direct Input)', '生成输出 Token (Output)'],
        textStyle: { color: '#94a3b8' },
        top: 0
      },
      grid: { left: '3%', right: '4%', bottom: '8%', top: '15%', containLabel: true },
      xAxis: {
        type: 'category',
        boundaryGap: false,
        data: dates,
        axisLine: { lineStyle: { color: '#334155' } },
        axisLabel: { color: '#94a3b8', fontSize: 11 }
      },
      yAxis: {
        type: 'value',
        axisLine: { lineStyle: { color: '#334155' } },
        splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
        axisLabel: {
          color: '#94a3b8',
          formatter: val => formatTokens(val)
        }
      },
      series: [
        {
          name: '缓存命中 Token (Cached Input)',
          type: 'line',
          stack: 'Total',
          smooth: true,
          areaStyle: { opacity: 0.6, color: 'rgba(160, 230, 200, 0.4)' },
          itemStyle: { color: '#10b981' },
          data: sorted.map(d => d.cached_input_tokens)
        },
        {
          name: '直读输入 Token (Direct Input)',
          type: 'line',
          stack: 'Total',
          smooth: true,
          areaStyle: { opacity: 0.6, color: 'rgba(56, 189, 248, 0.4)' },
          itemStyle: { color: '#38bdf8' },
          data: sorted.map(d => Math.max(0, (d.input_tokens || 0) - (d.cached_input_tokens || 0)))
        },
        {
          name: '生成输出 Token (Output)',
          type: 'line',
          stack: 'Total',
          smooth: true,
          areaStyle: { opacity: 0.6, color: 'rgba(168, 85, 247, 0.4)' },
          itemStyle: { color: '#a855f7' },
          data: sorted.map(d => d.output_tokens)
        }
      ]
    };
  }, [timeseries]);

  // ECharts 3: 模型分布饼图
  const modelPieOption = useMemo(() => {
    const data = models.map(m => ({
      name: m.model,
      value: (m.input_tokens || 0) + (m.output_tokens || 0),
      calls: m.model_calls
    })).filter(d => d.value > 0);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'item',
        formatter: params => {
          return `<b>${params.name}</b><br/>Token 消耗: ${formatTokens(params.value)} (${params.percent}%)<br/>调用次数: ${formatNumber(params.data.calls)}`;
        }
      },
      legend: {
        orient: 'vertical',
        right: '5%',
        top: 'center',
        textStyle: { color: '#94a3b8', fontSize: 11 }
      },
      series: [
        {
          name: '模型 Token 占比',
          type: 'pie',
          radius: ['45%', '70%'],
          center: ['35%', '50%'],
          avoidLabelOverlap: false,
          itemStyle: {
            borderRadius: 6,
            borderColor: '#0f172a',
            borderWidth: 2
          },
          label: { show: false },
          emphasis: {
            label: {
              show: true,
              fontSize: 13,
              fontWeight: 'bold',
              color: '#f8fafc',
              formatter: '{b}\n{d}%'
            }
          },
          data: data.length > 0 ? data : [{ name: '无模型数据', value: 0 }]
        }
      ]
    };
  }, [models]);

  // ECharts 4: 工具排行
  const toolBarOption = useMemo(() => {
    const topTools = [...tools].slice(0, 10).reverse();
    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'shadow' }
      },
      grid: { left: '3%', right: '8%', bottom: '5%', top: '5%', containLabel: true },
      xAxis: {
        type: 'value',
        axisLine: { lineStyle: { color: '#334155' } },
        splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
        axisLabel: { color: '#94a3b8' }
      },
      yAxis: {
        type: 'category',
        data: topTools.map(t => t.detail_name || t.tool_name),
        axisLine: { lineStyle: { color: '#334155' } },
        axisLabel: { color: '#cbd5e1', fontSize: 11 }
      },
      series: [
        {
          name: '普通工具调用',
          type: 'bar',
          stack: 'total',
          itemStyle: { color: '#3b82f6', borderRadius: [0, 0, 0, 0] },
          data: topTools.map(t => (t.calls || 0) - (t.mcp_calls || 0))
        },
        {
          name: 'MCP Server 调用',
          type: 'bar',
          stack: 'total',
          itemStyle: { color: '#ec4899', borderRadius: [0, 4, 4, 0] },
          data: topTools.map(t => t.mcp_calls || 0)
        }
      ]
    };
  }, [tools]);

  // ECharts 5: 代码分类饼图
  const codeCategoryPieOption = useMemo(() => {
    if (!codeStats) return {};
    const data = [
      { name: '业务核心代码 (Business)', value: codeStats.business_lines_added, itemStyle: { color: '#3b82f6' } },
      { name: '单元与集成测试 (Test)', value: codeStats.test_lines_added, itemStyle: { color: '#10b981' } },
      { name: '工程与接口文档 (Doc)', value: codeStats.documentation_lines_added, itemStyle: { color: '#a855f7' } },
      { name: '脚手架与配置 (Other)', value: codeStats.other_lines_added, itemStyle: { color: '#64748b' } }
    ].filter(d => d.value > 0);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'item',
        formatter: '{b}<br/>新增行数: {c} 行 ({d}%)'
      },
      legend: {
        orient: 'vertical',
        right: '5%',
        top: 'center',
        textStyle: { color: '#94a3b8' }
      },
      series: [
        {
          name: '代码类型分布',
          type: 'pie',
          radius: ['45%', '70%'],
          center: ['35%', '50%'],
          itemStyle: {
            borderRadius: 6,
            borderColor: '#0f172a',
            borderWidth: 2
          },
          label: { show: false },
          data: data.length > 0 ? data : [{ name: '暂无数据', value: 0 }]
        }
      ]
    };
  }, [codeStats]);

  // ECharts 6: 项目采纳对比柱状图
  const projectChartOption = useMemo(() => {
    const sorted = [...projects].slice(0, 8);
    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'shadow' }
      },
      legend: {
        data: ['候选行数 (Candidate)', 'Git 采纳行数 (Accepted)', '采纳率 %'],
        textStyle: { color: '#94a3b8' },
        top: 0
      },
      grid: { left: '3%', right: '4%', bottom: '10%', top: '15%', containLabel: true },
      xAxis: {
        type: 'category',
        data: sorted.map(p => p.project),
        axisLine: { lineStyle: { color: '#334155' } },
        axisLabel: { color: '#cbd5e1', fontSize: 11, interval: 0, rotate: sorted.length > 5 ? 20 : 0 }
      },
      yAxis: [
        {
          type: 'value',
          name: '代码行数',
          nameTextStyle: { color: '#94a3b8' },
          axisLine: { lineStyle: { color: '#334155' } },
          splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
          axisLabel: { color: '#94a3b8' }
        },
        {
          type: 'value',
          name: '采纳率 %',
          min: 0,
          max: 100,
          nameTextStyle: { color: '#94a3b8' },
          axisLine: { lineStyle: { color: '#334155' } },
          splitLine: { show: false },
          axisLabel: { color: '#94a3b8', formatter: '{value}%' }
        }
      ],
      series: [
        {
          name: '候选行数 (Candidate)',
          type: 'bar',
          itemStyle: { color: '#3b82f6', borderRadius: [4, 4, 0, 0] },
          data: sorted.map(p => p.candidate_lines || p.lines_added || 0)
        },
        {
          name: 'Git 采纳行数 (Accepted)',
          type: 'bar',
          itemStyle: { color: '#10b981', borderRadius: [4, 4, 0, 0] },
          data: sorted.map(p => p.accepted_lines || 0)
        },
        {
          name: '采纳率 %',
          type: 'line',
          yAxisIndex: 1,
          smooth: true,
          itemStyle: { color: '#38bdf8' },
          lineStyle: { width: 3 },
          data: sorted.map(p => (p.acceptance_rate || 0).toFixed(2))
        }
      ]
    };
  }, [projects]);

  const totalTokens = summary ? (summary.total_input_tokens + summary.total_output_tokens) : 0;
  const acceptanceRate = attribution ? attribution.acceptance_rate : 0;
  const businessShare = codeStats ? codeStats.business_code_share : 0;

  return (
    <ConfigProvider
      locale={zhCN}
      theme={{
        algorithm: theme.darkAlgorithm,
        token: {
          colorPrimary: '#0284c7',
          colorBgBase: '#090d16',
          colorBgContainer: '#0f172a',
          colorBgElevated: '#1e293b',
          colorBorder: 'rgba(255, 255, 255, 0.1)',
          colorBorderSecondary: 'rgba(255, 255, 255, 0.06)',
          borderRadius: 8,
          fontFamily: "'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif"
        }
      }}
    >
      <Layout style={{ minHeight: '100vh', background: '#090d16' }}>
        <Header style={{
          background: 'rgba(15, 23, 42, 0.75)',
          backdropFilter: 'blur(12px)',
          borderBottom: '1px solid rgba(255,255,255,0.08)',
          position: 'sticky',
          top: 0,
          zIndex: 100,
          padding: '0 28px',
          height: 'auto',
          lineHeight: 'normal'
        }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '14px 0', flexWrap: 'wrap', gap: 14 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
              <div style={{
                width: 40,
                height: 40,
                borderRadius: 10,
                background: 'linear-gradient(135deg, #0284c7, #38bdf8)',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                boxShadow: '0 4px 12px rgba(2, 132, 199, 0.35)'
              }}>
                <RobotOutlined style={{ fontSize: 22, color: '#fff' }} />
              </div>
              <div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                  <Title level={4} style={{ margin: 0, color: '#f8fafc', letterSpacing: '-0.02em' }}>
                    AgentStat 全景效能看板
                  </Title>
                  <Tag color="success" style={{ borderRadius: 12, padding: '1px 8px', fontSize: 11 }}>
                    <Badge status="processing" color="#10b981" /> Ant Design 5 驱动
                  </Tag>
                </div>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  Codex / Claude Code / Antigravity 统一工作流效能、Token 资产与 Git 代码采纳归因
                </Text>
              </div>
            </div>

            {/* Ant Design Header Controls */}
            <Space size="middle" wrap>
              <Segmented
                value={period}
                onChange={setPeriod}
                options={[
                  { label: '每日趋势 (30天)', value: 'day', icon: <CalendarOutlined /> },
                  { label: '每周趋势 (12周)', value: 'week', icon: <BarChartOutlined /> },
                  { label: '每月趋势 (12月)', value: 'month', icon: <HistoryOutlined /> }
                ]}
                style={{ background: '#1e293b' }}
              />

              <Button
                type="primary"
                icon={<SyncOutlined spin={syncing} />}
                onClick={triggerSync}
                loading={syncing}
                style={{ background: '#0284c7', borderColor: '#38bdf8', fontWeight: 500 }}
              >
                {syncing ? '同步中' : '同步日志'}
              </Button>

              <Button
                icon={<SyncOutlined spin={loading && !syncing} />}
                onClick={fetchAllData}
                style={{ background: '#1e293b' }}
              >
                刷新
              </Button>

              <Button
                icon={<DownloadOutlined />}
                onClick={exportCSV}
                style={{ background: '#1e293b' }}
              >
                导出 CSV
              </Button>
            </Space>
          </div>
        </Header>

        <Content style={{ padding: '20px 28px 60px 28px', maxWidth: 1680, margin: '0 auto', width: '100%' }}>
          <Spin spinning={loading} tip="正在计算全局效能统计指标...">
            {/* Ant Design RangePicker Toolbar */}
            <Card
              size="small"
              style={{
                marginBottom: 20,
                background: '#0f172a',
                borderColor: 'rgba(255,255,255,0.08)',
                boxShadow: '0 4px 20px -2px rgba(0,0,0,0.4)'
              }}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 12 }}>
                <Space size="middle" align="center">
                  <span style={{ fontWeight: 600, color: '#94a3b8', fontSize: 13, display: 'flex', alignItems: 'center', gap: 6 }}>
                    <ClockCircleOutlined style={{ color: '#38bdf8' }} /> 时间范围筛选:
                  </span>
                  <RangePicker
                    presets={rangePresets}
                    value={dateRange}
                    onChange={(dates) => setDateRange(dates)}
                    style={{ minWidth: 260, background: '#1e293b' }}
                    allowClear={true}
                  />
                  {dateRange && (
                    <Text type="secondary" style={{ fontSize: 12 }}>
                      已选: <span style={{ color: '#38bdf8', fontWeight: 600 }}>{dateRange[0]?.format('YYYY-MM-DD')}</span> 至 <span style={{ color: '#38bdf8', fontWeight: 600 }}>{dateRange[1]?.format('YYYY-MM-DD')}</span>
                    </Text>
                  )}
                </Space>

                <Space size="small">
                  <Button
                    size="small"
                    type={autoRefresh ? 'primary' : 'default'}
                    onClick={() => setAutoRefresh(!autoRefresh)}
                    style={{ fontSize: 12 }}
                  >
                    {autoRefresh ? '🟢 自动刷新 (30s)' : '⚪ 开启自动轮询'}
                  </Button>
                </Space>
              </div>
            </Card>

            {/* 8-Card KPI Grid with Ant Design Tooltips */}
            <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      总会话记录
                      <MetricTip
                        title="总会话记录 (Sessions)"
                        description="统计选定时间区间内，由 Codex、Claude Code 或 Antigravity 发起并记录的独立交互会话总数。"
                        formula="COUNT(DISTINCT session_id)"
                      />
                    </Text>
                    <RobotOutlined style={{ color: '#38bdf8', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#f8fafc' }}>
                    {formatNumber(summary?.total_sessions)}
                    <span style={{ fontSize: 12, fontWeight: 400, color: '#94a3b8', marginLeft: 4 }}>次</span>
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    覆盖 <span style={{ color: '#38bdf8', fontWeight: 600 }}>{usage?.sources?.length || 3} 个 Agent</span>
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      周期总 Token
                      <MetricTip
                        title="周期输入 / 输出总 Token"
                        description="选定时间范围内消耗的模型 Token 总和，包含 Prompt 缓存命中、直接输入与生成输出。"
                        formula="SUM(input_tokens + output_tokens)"
                      />
                    </Text>
                    <ThunderboltOutlined style={{ color: '#fbbf24', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#f8fafc' }}>
                    {formatTokens(totalTokens)}
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    入: <span style={{ color: '#38bdf8' }}>{formatTokens(usage?.input_tokens)}</span> · 出: <span style={{ color: '#a855f7' }}>{formatTokens(usage?.output_tokens)}</span>
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      精确代码采纳率
                      <MetricTip
                        title="AI 代码精确采纳率 (双维度)"
                        description="基于 SHA-256 代码行哈希指纹，比对 Agent 提议代码行最终被 Git Commit 实际提交并合入代码库的比例；同时提供至少采纳了 1 行代码的会话占比。"
                        formula="Git 已采纳代码行 / Agent 提议候选行 × 100%"
                      />
                    </Text>
                    <AimOutlined style={{ color: '#38bdf8', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#38bdf8' }}>
                    {acceptanceRate.toFixed(1)}%
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    已采纳 <span style={{ color: '#10b981', fontWeight: 600 }}>{formatNumber(attribution?.accepted_lines)}</span> 行 (会话: {(attribution?.session_acceptance_rate || 0).toFixed(1)}%)
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      代码净产出
                      <MetricTip
                        title="代码净产出与 Git 合并占比"
                        description="统计 Agent 新增与删除的代码行数；Git 最终合并占比反映仓库全量代码提交中来自 AI 编写的贡献份额。"
                        formula="已采纳 AI 行 / Git 仓库总新增代码行 × 100%"
                      />
                    </Text>
                    <CodeOutlined style={{ color: '#10b981', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 22, fontWeight: 700 }}>
                    <span style={{ color: '#10b981' }}>+{formatNumber(codeStats?.lines_added)}</span>
                    <span style={{ fontSize: 14, color: '#f43f5e', marginLeft: 6 }}>-{formatNumber(codeStats?.lines_deleted)}</span>
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    Git 合并占比: <span style={{ color: '#38bdf8', fontWeight: 600 }}>{(attribution?.ai_git_merge_share || 0).toFixed(1)}%</span>
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      单成功任务消耗
                      <MetricTip
                        title="成功任务消耗与失败 Token 占比"
                        description="单成功任务 Token 衡量产出代码的会话平均模型开销；失败会话 Token 占比衡量因中断或未产出变更而浪费的资源比例。"
                        formula="失败会话 Token / 全量 Token × 100%"
                      />
                    </Text>
                    <FireOutlined style={{ color: '#f97316', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 22, fontWeight: 700, color: '#f8fafc' }}>
                    {formatTokens(usage?.avg_tokens_per_successful_session || 0)}
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    失败/空跑占比: <span style={{ color: (usage?.failed_token_ratio || 0) > 20 ? '#f43f5e' : '#34d399', fontWeight: 600 }}>{(usage?.failed_token_ratio || 0).toFixed(1)}%</span>
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      单会话工具数
                      <MetricTip
                        title="工具调用频度与成功率"
                        description="衡量 Agent 自主解决问题时对终端命令、文件读写、MCP Server 等工具的调用频度，以及工具正常返回无异常的比例。"
                        formula="成功工具调用 / 总工具调用 × 100%"
                      />
                    </Text>
                    <ToolOutlined style={{ color: '#a855f7', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#f8fafc' }}>
                    {(usage?.avg_tools_per_session || 0).toFixed(1)}
                    <span style={{ fontSize: 12, fontWeight: 400, color: '#94a3b8', marginLeft: 4 }}>次/会话</span>
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    成功率: <span style={{ color: '#34d399', fontWeight: 600 }}>{(usage?.tool_success_rate || 98.8).toFixed(1)}%</span> (共 {formatNumber(usage?.tool_calls)} 次)
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      缓存命中率
                      <MetricTip
                        title="Prompt 缓存命中率 (Cache Hit Rate)"
                        description="统计通过 Prompt Caching 复用上下文的 Token 比例，缓存命中可显著降低模型响应延迟与账单成本。"
                        formula="cached_input_tokens / input_tokens × 100%"
                      />
                    </Text>
                    <DatabaseOutlined style={{ color: '#10b981', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#10b981' }}>
                    {(usage?.cache_hit_rate || 0).toFixed(1)}%
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    复用: <span style={{ color: '#10b981', fontWeight: 600 }}>{formatTokens(usage?.cached_input_tokens)}</span> Token
                  </Text>
                </Card>
              </Col>

              <Col xs={24} sm={12} md={6} lg={6} xl={3}>
                <Card bordered={false} style={{ background: '#0f172a', border: '1px solid rgba(255,255,255,0.08)', height: '100%' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                    <Text type="secondary" style={{ fontSize: 12, fontWeight: 600 }}>
                      估算总费用
                      <MetricTip
                        title="估算模型调用总费用"
                        description="根据在“模型与 Token 透视”中配置的各模型定价费率，汇总计算模型调用的实际 USD 成本。"
                        formula="∑ (各模型 Token × 单价 / 1,000,000)"
                      />
                    </Text>
                    <DollarCircleOutlined style={{ color: '#eab308', fontSize: 16 }} />
                  </div>
                  <div style={{ fontSize: 24, fontWeight: 700, color: '#facc15' }}>
                    ${(summary?.estimated_cost_usd || 0).toFixed(4)}
                  </div>
                  <Text type="secondary" style={{ fontSize: 12, marginTop: 4, display: 'block' }}>
                    覆盖率: <span style={{ color: '#38bdf8' }}>{(summary?.cost_coverage || 0).toFixed(1)}%</span>
                  </Text>
                </Card>
              </Col>
            </Row>

            {/* Main Tabs System */}
            <Tabs
              activeKey={activeTab}
              onChange={setActiveTab}
              type="card"
              items={[
                {
                  key: 'efficiency',
                  label: <span><BarChartOutlined /> 工作效率与产出趋势</span>,
                  children: (
                    <Space direction="vertical" size="large" style={{ width: '100%' }}>
                      <Card
                        title={
                          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <span>📊 个人工作效率与代码产出趋势</span>
                            <Text type="secondary" style={{ fontSize: 12, fontWeight: 400 }}>
                              统计{period === 'day' ? '日' : period === 'week' ? '周' : '月'}维度新增/删除代码行、会话量与工具调用频次
                            </Text>
                          </div>
                        }
                        style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
                      >
                        <EChartComponent option={efficiencyChartOption} height="380px" />
                      </Card>

                      <Card
                        title={
                          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                            <span>⚡ Token 资产消耗与缓存复用趋势</span>
                            <Text type="secondary" style={{ fontSize: 12, fontWeight: 400 }}>
                              对比 Prompt 缓存读取、直读输入与生成输出 Token 分布
                            </Text>
                          </div>
                        }
                        style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
                      >
                        <EChartComponent option={tokenChartOption} height="380px" />
                      </Card>
                    </Space>
                  )
                },
                {
                  key: 'models',
                  label: <span><PieChartOutlined /> 模型与 Token 透视 <Badge count={models.length} overflowCount={99} style={{ backgroundColor: '#1e293b', color: '#38bdf8' }} /></span>,
                  children: (
                    <Row gutter={[16, 16]}>
                      <Col xs={24} lg={10}>
                        <Card title="🤖 模型 Token 消耗分布占比" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)', height: '100%' }}>
                          <EChartComponent option={modelPieOption} height="340px" />
                        </Card>
                      </Col>
                      <Col xs={24} lg={14}>
                        <Card
                          title="📋 模型调用与定价配置台账"
                          extra={<Button size="small" type="primary" onClick={() => handleOpenPricing(models[0] || { source: 'claude', model: 'claude-3-5-sonnet' })}>配置模型单价</Button>}
                          style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
                        >
                          <Table
                            dataSource={models}
                            rowKey={(r) => `${r.source}_${r.model}`}
                            pagination={{ pageSize: 6 }}
                            size="small"
                            columns={[
                              { title: '来源', dataIndex: 'source', render: s => <Tag color={s === 'claude' ? 'orange' : s === 'codex' ? 'green' : 'blue'}>{s}</Tag> },
                              { title: '模型名称', dataIndex: 'model', render: m => <Text code>{m}</Text> },
                              { title: '调用次数', dataIndex: 'model_calls', render: v => formatNumber(v) },
                              { title: '总 Token', render: (_, r) => formatTokens((r.input_tokens || 0) + (r.output_tokens || 0)) },
                              { title: '缓存读取', dataIndex: 'cached_input_tokens', render: v => formatTokens(v) },
                              { title: '估算费用', render: (_, r) => r.pricing_configured ? `$${(r.estimated_cost_usd || 0).toFixed(4)}` : <Text type="secondary">未定价</Text> },
                              {
                                title: '操作',
                                render: (_, r) => (
                                  <Button type="link" size="small" onClick={() => handleOpenPricing(r)}>
                                    定价
                                  </Button>
                                )
                              }
                            ]}
                          />
                        </Card>
                      </Col>
                    </Row>
                  )
                },
                {
                  key: 'attribution',
                  label: <span><AimOutlined /> 代码分类与 Git 归因</span>,
                  children: (
                    <Row gutter={[16, 16]}>
                      <Col xs={24} lg={10}>
                        <Card title="💻 AI 代码类别结构分布" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                          <EChartComponent option={codeCategoryPieOption} height="340px" />
                        </Card>
                      </Col>
                      <Col xs={24} lg={14}>
                        <Card title="🎯 精确行级别 Git 采纳归因明细" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                          <Table
                            dataSource={[
                              { key: 'total', name: '全量代码 (Overall)', cand: attribution?.candidate_lines || 0, acc: attribution?.accepted_lines || 0, rate: attribution?.acceptance_rate || 0 },
                              { key: 'biz', name: '业务代码 (Business Logic)', cand: attribution?.business_candidate_lines || 0, acc: attribution?.business_accepted_lines || 0, rate: attribution?.business_acceptance_rate || 0 },
                              { key: 'test', name: '测试代码 (Unit/Integration Test)', cand: attribution?.test_candidate_lines || 0, acc: attribution?.test_accepted_lines || 0, rate: attribution?.test_candidate_lines ? (attribution.test_accepted_lines * 100 / attribution.test_candidate_lines) : 0 },
                              { key: 'doc', name: '文档与注释 (Documentation)', cand: attribution?.documentation_candidate_lines || 0, acc: attribution?.documentation_accepted_lines || 0, rate: attribution?.documentation_candidate_lines ? (attribution.documentation_accepted_lines * 100 / attribution.documentation_candidate_lines) : 0 }
                            ]}
                            pagination={false}
                            size="middle"
                            columns={[
                              { title: '代码分类', dataIndex: 'name', render: t => <Text strong>{t}</Text> },
                              { title: 'Agent 提议候选行', dataIndex: 'cand', render: v => `${formatNumber(v)} 行` },
                              { title: 'Git 实际采纳行', dataIndex: 'acc', render: v => <span style={{ color: '#10b981', fontWeight: 600 }}>{formatNumber(v)} 行</span> },
                              {
                                title: '采纳率',
                                dataIndex: 'rate',
                                render: v => (
                                  <Space>
                                    <Progress percent={Number(v.toFixed(1))} size="small" style={{ width: 100 }} />
                                    <Text style={{ fontWeight: 600 }}>{v.toFixed(1)}%</Text>
                                  </Space>
                                )
                              }
                            ]}
                          />
                        </Card>
                      </Col>
                    </Row>
                  )
                },
                {
                  key: 'tools',
                  label: <span><ToolOutlined /> 工具 / MCP / Skills <Badge count={tools.length} overflowCount={99} style={{ backgroundColor: '#1e293b', color: '#a855f7' }} /></span>,
                  children: (
                    <Row gutter={[16, 16]}>
                      <Col xs={24} lg={12}>
                        <Card title="🛠️ Top 10 工具调用排行榜" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                          <EChartComponent option={toolBarOption} height="360px" />
                        </Card>
                      </Col>
                      <Col xs={24} lg={12}>
                        <Card title="🔌 MCP Server 与 Skills 扩展调用清单" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                          <Table
                            dataSource={[...mcp.map(m => ({ ...m, type: 'MCP' })), ...skills.map(s => ({ ...s, type: 'Skill' }))]}
                            rowKey={(r) => `${r.type}_${r.name}_${r.source}`}
                            pagination={{ pageSize: 6 }}
                            size="small"
                            columns={[
                              { title: '类型', dataIndex: 'type', render: t => <Tag color={t === 'MCP' ? 'purple' : 'cyan'}>{t}</Tag> },
                              { title: '能力名称', dataIndex: 'name', render: n => <Text strong code>{n}</Text> },
                              { title: '功能细节', dataIndex: 'detail' },
                              { title: '来源', dataIndex: 'source', render: s => <Tag>{s}</Tag> },
                              { title: '调用频次', dataIndex: 'calls', render: c => formatNumber(c) }
                            ]}
                          />
                        </Card>
                      </Col>
                    </Row>
                  )
                },
                {
                  key: 'projects',
                  label: <span><FolderOutlined /> 项目工作区 <Badge count={projects.length} overflowCount={99} style={{ backgroundColor: '#1e293b', color: '#38bdf8' }} /></span>,
                  children: (
                    <Space direction="vertical" size="large" style={{ width: '100%' }}>
                      <Card title="📊 项目工作区效能与采纳率对比" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                        <EChartComponent option={projectChartOption} height="360px" />
                      </Card>

                      <Card title="📁 各项目代码贡献与 Token 消耗明细" style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}>
                        <Table
                          dataSource={projects}
                          rowKey="project_path"
                          pagination={{ pageSize: 8 }}
                          size="middle"
                          columns={[
                            { title: '项目名称', dataIndex: 'project', render: p => <Text strong>{p}</Text> },
                            { title: '完整路径', dataIndex: 'project_path', render: p => <Text ellipsis copyable style={{ maxWidth: 300 }}>{p}</Text> },
                            { title: '会话数', dataIndex: 'sessions', render: v => formatNumber(v) },
                            { title: '输入/输出 Token', render: (_, r) => formatTokens((r.input_tokens || 0) + (r.output_tokens || 0)) },
                            { title: '新增/删除行', render: (_, r) => <span style={{ color: '#10b981' }}>+{formatNumber(r.lines_added)} <span style={{ color: '#f43f5e' }}>-{formatNumber(r.lines_deleted)}</span></span> },
                            { title: 'Git 采纳行', dataIndex: 'accepted_lines', render: v => `${formatNumber(v)} 行` },
                            {
                              title: '精确采纳率',
                              dataIndex: 'acceptance_rate',
                              render: v => <Progress percent={Number((v || 0).toFixed(1))} size="small" style={{ width: 90 }} />
                            }
                          ]}
                        />
                      </Card>
                    </Space>
                  )
                },
                {
                  key: 'sessions',
                  label: <span><FileTextOutlined /> 详细会话流水台账 <Badge count={sessions.length} overflowCount={999} style={{ backgroundColor: '#1e293b', color: '#10b981' }} /></span>,
                  children: (
                    <Card
                      title="📜 Agent 独立交互会话详细流水台账"
                      extra={
                        <Space wrap>
                          <Select
                            value={sessionAgentFilter}
                            onChange={setSessionAgentFilter}
                            style={{ width: 130 }}
                            options={[
                              { label: '全部来源', value: 'all' },
                              { label: 'Claude Code', value: 'claude' },
                              { label: 'Codex CLI', value: 'codex' },
                              { label: 'Antigravity', value: 'antigravity' }
                            ]}
                          />
                          <Input
                            placeholder="搜索 Session ID / 路径 / 模型"
                            prefix={<SearchOutlined />}
                            value={sessionSearch}
                            onChange={e => setSessionSearch(e.target.value)}
                            style={{ width: 260 }}
                            allowClear
                          />
                        </Space>
                      }
                      style={{ background: '#0f172a', borderColor: 'rgba(255,255,255,0.08)' }}
                    >
                      <Table
                        dataSource={filteredSessions}
                        rowKey="session_id"
                        pagination={{
                          current: sessionPage,
                          pageSize: pageSize,
                          onChange: (p, ps) => { setSessionPage(p); setPageSize(ps); },
                          showSizeChanger: true,
                          showTotal: (total) => `共 ${total} 条会话记录`
                        }}
                        size="middle"
                        columns={[
                          {
                            title: '会话 ID',
                            dataIndex: 'session_id',
                            render: id => (
                              <Text code copyable style={{ fontSize: 12 }}>
                                {id ? id.substring(0, 12) + '...' : '-'}
                              </Text>
                            )
                          },
                          {
                            title: '来源 Agent',
                            dataIndex: 'source',
                            render: s => (
                              <Tag color={s === 'claude' ? 'orange' : s === 'codex' ? 'green' : 'blue'}>
                                {s}
                              </Tag>
                            )
                          },
                          {
                            title: '开始时间',
                            dataIndex: 'started_at',
                            render: t => <span style={{ fontSize: 12, color: '#94a3b8' }}>{t || '-'}</span>
                          },
                          {
                            title: '工作目录',
                            dataIndex: 'cwd',
                            render: cwd => <Text ellipsis style={{ maxWidth: 220, fontSize: 12 }} title={cwd}>{cwd || '-'}</Text>
                          },
                          {
                            title: '使用模型',
                            dataIndex: 'models',
                            render: m => <Text ellipsis code style={{ maxWidth: 160, fontSize: 11 }} title={m}>{m || '-'}</Text>
                          },
                          {
                            title: 'Token 消耗',
                            render: (_, r) => formatTokens((r.input_tokens || 0) + (r.output_tokens || 0))
                          },
                          {
                            title: '工具调用',
                            dataIndex: 'tool_calls',
                            render: v => formatNumber(v)
                          },
                          {
                            title: '代码变更',
                            dataIndex: 'code_changes',
                            render: v => formatNumber(v)
                          },
                          {
                            title: '操作',
                            render: (_, r) => (
                              <Button
                                type="link"
                                size="small"
                                icon={<FileTextOutlined />}
                                onClick={() => setSelectedSessionId(r.session_id)}
                              >
                                查看对话流水
                              </Button>
                            )
                          }
                        ]}
                      />
                    </Card>
                  )
                }
              ]}
            />
          </Spin>
        </Content>

        {/* 会话流水详情 Drawer 弹窗 */}
        <TranscriptModal
          sessionId={selectedSessionId}
          onClose={() => setSelectedSessionId(null)}
        />

        {/* 模型定价抽屉 */}
        <Drawer
          title={`配置模型定价费率: ${selectedPricingModel?.model || ''}`}
          placement="right"
          onClose={() => setPricingDrawerOpen(false)}
          open={pricingDrawerOpen}
          width={400}
          extra={
            <Button type="primary" onClick={handleSavePricing}>
              保存费率
            </Button>
          }
        >
          <Paragraph type="secondary">
            为该模型设置每百万 Token（1M Tokens）的标准计费单价（USD）。保存后系统将自动重算所有历史会话的模型消耗成本。
          </Paragraph>
          <Divider />
          <Space direction="vertical" size="middle" style={{ width: '100%' }}>
            <div>
              <Text strong>直接输入单价 ($ / 1M Tokens)</Text>
              <Input
                type="number"
                step="0.01"
                value={pricingForm.input_rate}
                onChange={e => setPricingForm({ ...pricingForm, input_rate: parseFloat(e.target.value) || 0 })}
                style={{ marginTop: 6 }}
              />
            </div>
            <div>
              <Text strong>缓存读取单价 ($ / 1M Tokens)</Text>
              <Input
                type="number"
                step="0.01"
                value={pricingForm.cache_read_rate}
                onChange={e => setPricingForm({ ...pricingForm, cache_read_rate: parseFloat(e.target.value) || 0 })}
                style={{ marginTop: 6 }}
              />
            </div>
            <div>
              <Text strong>缓存写入单价 ($ / 1M Tokens)</Text>
              <Input
                type="number"
                step="0.01"
                value={pricingForm.cache_write_rate}
                onChange={e => setPricingForm({ ...pricingForm, cache_write_rate: parseFloat(e.target.value) || 0 })}
                style={{ marginTop: 6 }}
              />
            </div>
            <div>
              <Text strong>生成输出单价 ($ / 1M Tokens)</Text>
              <Input
                type="number"
                step="0.01"
                value={pricingForm.output_rate}
                onChange={e => setPricingForm({ ...pricingForm, output_rate: parseFloat(e.target.value) || 0 })}
                style={{ marginTop: 6 }}
              />
            </div>
          </Space>
        </Drawer>
      </Layout>
    </ConfigProvider>
  );
}
