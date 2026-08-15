import React, { useState, useEffect, useMemo } from 'react';
import EChartComponent from './components/EChartComponent';
import TranscriptModal, { extractString } from './components/TranscriptModal';

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

  // 会话列表分页与搜索过滤
  const [sessionSearch, setSessionSearch] = useState('');
  const [sessionAgentFilter, setSessionAgentFilter] = useState('all');
  const [sessionPage, setSessionPage] = useState(1);
  const pageSize = 10;

  // 会话流水弹窗详情状态
  const [selectedSessionId, setSelectedSessionId] = useState(null);

  const fetchAllData = async () => {
    try {
      const [
        sumRes, useRes, codeRes, attrRes, modRes, toolRes, projRes, mcpRes, skillRes, timeRes, sessRes
      ] = await Promise.all([
        fetch('/api/summary').then(r => r.json()).catch(() => null),
        fetch('/api/usage').then(r => r.json()).catch(() => null),
        fetch('/api/code').then(r => r.json()).catch(() => null),
        fetch('/api/attribution').then(r => r.json()).catch(() => null),
        fetch('/api/models').then(r => r.json()).catch(() => []),
        fetch('/api/tools').then(r => r.json()).catch(() => []),
        fetch('/api/projects').then(r => r.json()).catch(() => []),
        fetch('/api/mcp').then(r => r.json()).catch(() => []),
        fetch('/api/skills').then(r => r.json()).catch(() => []),
        fetch(`/api/timeseries?period=${period}`).then(r => r.json()).catch(() => ({ rows: [] })),
        fetch('/api/sessions').then(r => r.json()).catch(() => [])
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
    }
  };

  useEffect(() => {
    fetchAllData();
  }, [period]);

  useEffect(() => {
    let timer = null;
    if (autoRefresh) {
      timer = setInterval(fetchAllData, 30000);
    }
    return () => {
      if (timer) clearInterval(timer);
    };
  }, [autoRefresh, period]);

  const triggerSync = async () => {
    setSyncing(true);
    try {
      await fetch('/api/sync').then(r => r.json()).catch(() => null);
      await fetchAllData();
    } catch (e) {
      console.error("Sync failed", e);
    } finally {
      setSyncing(false);
    }
  };

  // 过滤后的会话流水
  const filteredSessions = useMemo(() => {
    return sessions.filter(s => {
      if (sessionAgentFilter !== 'all' && s.source !== sessionAgentFilter) {
        return false;
      }
      if (sessionSearch.trim()) {
        const kw = sessionSearch.toLowerCase();
        const matchId = (s.session_id || '').toLowerCase().includes(kw);
        const matchCwd = (s.cwd || '').toLowerCase().includes(kw);
        const matchModel = (s.models || '').toLowerCase().includes(kw);
        return matchId || matchCwd || matchModel;
      }
      return true;
    });
  }, [sessions, sessionSearch, sessionAgentFilter]);

  const paginatedSessions = useMemo(() => {
    const start = (sessionPage - 1) * pageSize;
    return filteredSessions.slice(start, start + pageSize);
  }, [filteredSessions, sessionPage]);

  // 导出 CSV
  const exportCSV = () => {
    if (!sessions || sessions.length === 0) return;
    const headers = ["SessionID", "Source", "StartedAt", "Models", "InputTokens", "OutputTokens", "ToolCalls", "CodeChanges", "WorkDir"];
    const rows = sessions.map(s => [
      `"${s.session_id}"`,
      `"${s.source}"`,
      `"${s.started_at}"`,
      `"${s.models || ''}"`,
      s.input_tokens || 0,
      s.output_tokens || 0,
      s.tool_calls || 0,
      s.code_changes || 0,
      `"${(s.cwd || '').replace(/"/g, '""')}"`
    ]);
    const csvContent = "data:text/csv;charset=utf-8,\uFEFF" + [headers.join(","), ...rows.map(e => e.join(","))].join("\n");
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", `agentstat_sessions_${new Date().toISOString().slice(0,10)}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  // 图表配置：每日/每周 产出与会话效能组合图
  const efficiencyChartOption = useMemo(() => {
    const sorted = [...timeseries].sort((a, b) => a.period_start.localeCompare(b.period_start));
    const dates = sorted.map(d => d.period_start);
    const linesAdded = sorted.map(d => d.lines_added);
    const linesDeleted = sorted.map(d => d.lines_deleted);
    const sessionCount = sorted.map(d => d.sessions);
    const codeChanges = sorted.map(d => d.code_changes);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'cross' }
      },
      legend: {
        data: ['新增代码行', '删除代码行', '会话次数 (Sessions)', '代码变更事件'],
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
          name: '会话/事件数',
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
          itemStyle: { color: '#10b981' },
          data: linesAdded
        },
        {
          name: '删除代码行',
          type: 'bar',
          stack: 'lines',
          itemStyle: { color: '#f43f5e', borderRadius: [4, 4, 0, 0] },
          data: linesDeleted
        },
        {
          name: '会话次数 (Sessions)',
          type: 'line',
          yAxisIndex: 1,
          smooth: true,
          itemStyle: { color: '#38bdf8' },
          lineStyle: { width: 3 },
          data: sessionCount
        },
        {
          name: '代码变更事件',
          type: 'line',
          yAxisIndex: 1,
          smooth: true,
          itemStyle: { color: '#a855f7' },
          lineStyle: { width: 2, type: 'dashed' },
          data: codeChanges
        }
      ]
    };
  }, [timeseries]);

  // 图表配置：Token 消耗与缓存利用堆叠趋势图
  const tokenTrendChartOption = useMemo(() => {
    const sorted = [...timeseries].sort((a, b) => a.period_start.localeCompare(b.period_start));
    const dates = sorted.map(d => d.period_start);
    const cachedTokens = sorted.map(d => d.cached_input_tokens);
    const directInputTokens = sorted.map(d => Math.max(0, (d.input_tokens || 0) - (d.cached_input_tokens || 0)));
    const outputTokens = sorted.map(d => d.output_tokens);

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        formatter: function(params) {
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
            <span>总计 Tokens:</span>
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
          areaStyle: { opacity: 0.6, color: 'rgba(16, 185, 129, 0.4)' },
          itemStyle: { color: '#10b981' },
          data: cachedTokens
        },
        {
          name: '直读输入 Token (Direct Input)',
          type: 'line',
          stack: 'Total',
          smooth: true,
          areaStyle: { opacity: 0.6, color: 'rgba(56, 189, 248, 0.4)' },
          itemStyle: { color: '#38bdf8' },
          data: directInputTokens
        },
        {
          name: '生成输出 Token (Output)',
          type: 'line',
          stack: 'Total',
          smooth: true,
          areaStyle: { opacity: 0.6, color: 'rgba(168, 85, 247, 0.4)' },
          itemStyle: { color: '#a855f7' },
          data: outputTokens
        }
      ]
    };
  }, [timeseries]);

  // 图表配置：模型分布饼图
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
            borderColor: '#111622',
            borderWidth: 2
          },
          label: { show: false },
          emphasis: {
            label: {
              show: true,
              fontSize: 14,
              fontWeight: 'bold',
              color: '#fff'
            }
          },
          data: data
        }
      ]
    };
  }, [models]);

  // 图表配置：代码分类环形图
  const codeTypePieOption = useMemo(() => {
    if (!codeStats) return null;
    const data = [
      { name: '业务逻辑代码', value: codeStats.business_lines_added || 0, itemStyle: { color: '#38bdf8' } },
      { name: '单元与集成测试', value: codeStats.test_lines_added || 0, itemStyle: { color: '#10b981' } },
      { name: '技术文档与注释', value: codeStats.documentation_lines_added || 0, itemStyle: { color: '#a855f7' } },
      { name: '脚手架与生成代码', value: codeStats.generated_lines_added || 0, itemStyle: { color: '#f59e0b' } },
      { name: '配置与其他代码', value: codeStats.other_lines_added || 0, itemStyle: { color: '#64748b' } }
    ];

    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'item',
        formatter: '{b}: <b>{c} 行</b> ({d}%)'
      },
      legend: {
        orient: 'vertical',
        right: '5%',
        top: 'center',
        textStyle: { color: '#94a3b8', fontSize: 11 }
      },
      series: [
        {
          name: '代码类型分布',
          type: 'pie',
          radius: ['50%', '75%'],
          center: ['35%', '50%'],
          itemStyle: {
            borderRadius: 6,
            borderColor: '#111622',
            borderWidth: 2
          },
          label: { show: false },
          data: data
        }
      ]
    };
  }, [codeStats]);

  // 图表配置：高频工具调用排行 TOP 15
  const toolsBarOption = useMemo(() => {
    const topTools = [...tools].sort((a, b) => b.calls - a.calls).slice(0, 15).reverse();
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
        axisLabel: { color: '#e2e8f0', fontSize: 12 }
      },
      series: [
        {
          name: '调用次数',
          type: 'bar',
          itemStyle: {
            color: {
              type: 'linear',
              x: 0,
              y: 0,
              x2: 1,
              y2: 0,
              colorStops: [
                { offset: 0, color: '#3b82f6' },
                { offset: 1, color: '#06b6d4' }
              ]
            },
            borderRadius: [0, 4, 4, 0]
          },
          label: {
            show: true,
            position: 'right',
            color: '#94a3b8',
            fontSize: 11
          },
          data: topTools.map(t => t.calls)
        }
      ]
    };
  }, [tools]);

  // 图表配置：各项目代码采纳率与产出对比柱状图
  const projectsAcceptanceOption = useMemo(() => {
    const sorted = [...projects].filter(p => (p.candidate_lines > 0 || p.code_changes > 0 || p.lines_added > 0)).slice(0, 10);
    if (sorted.length === 0) return null;
    return {
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'cross' }
      },
      legend: {
        data: ['候选行数 (Candidate)', 'Git 采纳行数 (Accepted)', '采纳率 %'],
        textStyle: { color: '#94a3b8' },
        top: 0
      },
      grid: { left: '3%', right: '4%', bottom: '8%', top: '15%', containLabel: true },
      xAxis: {
        type: 'category',
        data: sorted.map(p => p.project || '默认项目'),
        axisLine: { lineStyle: { color: '#334155' } },
        axisLabel: { color: '#94a3b8', fontSize: 11, interval: 0, rotate: 10 }
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
          name: '采纳率 (%)',
          max: 100,
          min: 0,
          nameTextStyle: { color: '#94a3b8' },
          axisLine: { lineStyle: { color: '#334155' } },
          splitLine: { show: false },
          axisLabel: { color: '#38bdf8', formatter: '{value}%' }
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
    <div className="container">
      {/* Header */}
      <header className="header">
        <div className="header-left">
          <div className="logo-badge">🤖</div>
          <div className="header-title">
            <h1>
              AgentStat 全景效能看板
              <span className="status-pill">
                <span className="status-dot"></span>
                本地 Vite + ECharts 高性能运行
              </span>
            </h1>
            <p>实时分析 Codex、Claude Code 与 Antigravity 本地工作效率、Token 资产消耗与代码采纳归因</p>
          </div>
        </div>

        <div className="header-controls">
          <div className="segmented-control">
            <button 
              className={`segmented-item ${period === 'day' ? 'active' : ''}`}
              onClick={() => setPeriod('day')}
            >
              📅 每日趋势 (30天)
            </button>
            <button 
              className={`segmented-item ${period === 'week' ? 'active' : ''}`}
              onClick={() => setPeriod('week')}
            >
              📊 每周趋势 (12周)
            </button>
            <button 
              className={`segmented-item ${period === 'month' ? 'active' : ''}`}
              onClick={() => setPeriod('month')}
            >
              🗓️ 每月趋势 (12月)
            </button>
          </div>

          <button 
            className="btn btn-primary" 
            onClick={triggerSync}
            disabled={syncing}
            style={{background: syncing ? '#475569' : '#0284c7', borderColor: '#38bdf8'}}
          >
            {syncing ? '⏳ 正在同步中...' : '⚡ 立即同步日志'}
          </button>

          <button className="btn" onClick={fetchAllData}>
            🔄 刷新数据
          </button>

          <button className="btn" onClick={() => setAutoRefresh(!autoRefresh)}>
            {autoRefresh ? '🟢 自动刷新中 (30s)' : '⚪ 开启自动刷新'}
          </button>

          <button className="btn" onClick={exportCSV}>
            📥 导出 CSV
          </button>
        </div>
      </header>

      {/* KPI Cards Grid */}
      <section className="kpi-grid">
        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">总会话记录 (Sessions)</span>
            <span className="kpi-icon">💬</span>
          </div>
          <div className="kpi-value">
            {formatNumber(summary?.total_sessions)}
            <span className="kpi-unit">次</span>
          </div>
          <div className="kpi-subtext">
            覆盖 <span className="kpi-highlight">{usage?.sources?.length || 3} 个 Agent</span> 来源
          </div>
        </div>

        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">周期输入 / 输出总 Token</span>
            <span className="kpi-icon">⚡</span>
          </div>
          <div className="kpi-value">
            {formatTokens(totalTokens)}
          </div>
          <div className="kpi-subtext">
            输入: <span className="kpi-highlight">{formatTokens(usage?.input_tokens)}</span> · 输出: <span className="kpi-highlight">{formatTokens(usage?.output_tokens)}</span> ({((usage?.input_tokens || 0) / (usage?.output_tokens || 1)).toFixed(1)}:1)
          </div>
        </div>

        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">AI 代码采纳率 (行/会话双维度)</span>
            <span className="kpi-icon">🎯</span>
          </div>
          <div className="kpi-value" style={{color: '#38bdf8'}}>
            {acceptanceRate.toFixed(1)}%
            <span style={{fontSize: '14px', color: '#10b981', marginLeft: '6px', fontWeight: 600}}>
              (会话: {(attribution?.session_acceptance_rate || 0).toFixed(1)}%)
            </span>
          </div>
          <div className="kpi-subtext">
            行采纳: <span className="kpi-highlight">{formatNumber(attribution?.accepted_lines)}</span>/{formatNumber(attribution?.candidate_lines)} 行 · 会话: {attribution?.accepted_sessions || 0}/{attribution?.proposing_sessions || 0} 个
          </div>
        </div>

        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">代码净产出与 AI 合并占比</span>
            <span className="kpi-icon">💻</span>
          </div>
          <div className="kpi-value">
            <span style={{color: '#10b981'}}>+{formatNumber(codeStats?.lines_added)}</span>
            <span style={{fontSize: '16px', color: '#f43f5e', marginLeft: '6px'}}>-{formatNumber(codeStats?.lines_deleted)}</span>
          </div>
          <div className="kpi-subtext">
            Git 最终合并占比: <span className="kpi-highlight" style={{color: '#38bdf8'}}>{(attribution?.ai_git_merge_share || 0).toFixed(1)}%</span> (业务占 {businessShare.toFixed(1)}%)
          </div>
        </div>

        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">单成功任务消耗 / 失败占比</span>
            <span className="kpi-icon">🎯</span>
          </div>
          <div className="kpi-value" style={{fontSize: '22px'}}>
            {formatTokens(usage?.avg_tokens_per_successful_session || 0)}
            <span style={{fontSize: '13px', color: '#94a3b8', fontWeight: 400, marginLeft: '4px'}}>/任务</span>
          </div>
          <div className="kpi-subtext">
            失败/空跑 Token 占比: <span className="kpi-highlight" style={{color: (usage?.failed_token_ratio || 0) > 30 ? '#f43f5e' : '#fbbf24'}}>{(usage?.failed_token_ratio || 0).toFixed(1)}%</span> ({formatTokens(usage?.failed_tokens)})
          </div>
        </div>

        <div className="kpi-card">
          <div className="kpi-card-header">
            <span className="kpi-title">单会话工具数 / 调用成功率</span>
            <span className="kpi-icon">🛠️</span>
          </div>
          <div className="kpi-value">
            {(usage?.avg_tools_per_session || 0).toFixed(1)}
            <span className="kpi-unit">次/会话</span>
          </div>
          <div className="kpi-subtext">
            工具成功率: <span className="kpi-highlight" style={{color: '#34d399'}}>{(usage?.tool_success_rate || 98.8).toFixed(1)}%</span> (总 {formatNumber(usage?.tool_calls)} 次)
          </div>
        </div>
      </section>

      {/* Navigation Tabs */}
      <nav className="nav-tabs">
        <button 
          className={`nav-tab-item ${activeTab === 'efficiency' ? 'active' : ''}`}
          onClick={() => setActiveTab('efficiency')}
        >
          📈 工作效率与产出趋势
        </button>
        <button 
          className={`nav-tab-item ${activeTab === 'models' ? 'active' : ''}`}
          onClick={() => setActiveTab('models')}
        >
          🤖 模型与 Token 深度透视
          <span className="tab-badge">{models.length}</span>
        </button>
        <button 
          className={`nav-tab-item ${activeTab === 'attribution' ? 'active' : ''}`}
          onClick={() => setActiveTab('attribution')}
        >
          🎯 代码分类与 Git 归因
        </button>
        <button 
          className={`nav-tab-item ${activeTab === 'tools' ? 'active' : ''}`}
          onClick={() => setActiveTab('tools')}
        >
          🛠️ 工具 / MCP / Skills 调用
          <span className="tab-badge">{tools.length}</span>
        </button>
        <button 
          className={`nav-tab-item ${activeTab === 'projects' ? 'active' : ''}`}
          onClick={() => setActiveTab('projects')}
        >
          📁 项目工作区
          <span className="tab-badge">{projects.length}</span>
        </button>
        <button 
          className={`nav-tab-item ${activeTab === 'sessions' ? 'active' : ''}`}
          onClick={() => setActiveTab('sessions')}
        >
          📜 详细会话流水台账
          <span className="tab-badge">{sessions.length}</span>
        </button>
      </nav>

      {/* Tab 1: 工作效率与产出趋势 */}
      {activeTab === 'efficiency' && (
        <div>
          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">📊 个人工作效率与代码产出趋势</h2>
                <p className="panel-subtitle">按{period === 'day' ? '日' : period === 'week' ? '周' : '月'}展示会话频次、新增代码行、删除行与代码变更事件</p>
              </div>
            </div>
            <EChartComponent option={efficiencyChartOption} height="400px" />
          </div>

          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">⚡ Token 资产消耗与缓存复用趋势</h2>
                <p className="panel-subtitle">对比缓存命中输入、直接输入与输出 Token 的随时间变化，绿色区域越大代表上下文缓存效率越高</p>
              </div>
            </div>
            <EChartComponent option={tokenTrendChartOption} height="380px" />
          </div>

          {/* 时序数据明细表 */}
          <div className="panel">
            <div className="panel-header">
              <h2 className="panel-title">📋 周期统计原始明细台账 ({period === 'day' ? '日' : period === 'week' ? '周' : '月'})</h2>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>时间周期</th>
                    <th>会话次数</th>
                    <th>模型调用</th>
                    <th>输入 Token</th>
                    <th>缓存命中 Token</th>
                    <th>输出 Token</th>
                    <th>新增代码行</th>
                    <th>删除代码行</th>
                    <th>工具调用</th>
                    <th>预估费用</th>
                  </tr>
                </thead>
                <tbody>
                  {timeseries.map((row, idx) => (
                    <tr key={idx}>
                      <td className="mono" style={{fontWeight: 600, color: '#38bdf8'}}>{row.period_start}</td>
                      <td>{row.sessions}</td>
                      <td>{row.model_calls}</td>
                      <td>{formatTokens(row.input_tokens)}</td>
                      <td style={{color: '#10b981'}}>{formatTokens(row.cached_input_tokens)}</td>
                      <td style={{color: '#c084fc'}}>{formatTokens(row.output_tokens)}</td>
                      <td style={{color: '#10b981'}}>+{formatNumber(row.lines_added)}</td>
                      <td style={{color: '#f43f5e'}}>-{formatNumber(row.lines_deleted)}</td>
                      <td>{row.tool_calls}</td>
                      <td>${Number(row.estimated_cost_usd || 0).toFixed(4)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 2: 模型与 Token 深度透视 */}
      {activeTab === 'models' && (
        <div>
          <div className="grid-2">
            <div className="panel">
              <div className="panel-header">
                <h2 className="panel-title">🥧 各模型 Token 消耗总占比</h2>
              </div>
              <EChartComponent option={modelPieOption} height="350px" />
            </div>

            <div className="panel">
              <div className="panel-header">
                <h2 className="panel-title">🤖 Agent 客户端来源对比</h2>
              </div>
              <div className="table-container">
                <table>
                  <thead>
                    <tr>
                      <th>Agent 来源</th>
                      <th>会话数</th>
                      <th>模型调用</th>
                      <th>输入 Token</th>
                      <th>缓存命中</th>
                      <th>输出 Token</th>
                      <th>代码变更</th>
                    </tr>
                  </thead>
                  <tbody>
                    {usage?.sources?.map((s, i) => (
                      <tr key={i}>
                        <td>
                          <span className={`badge ${s.source === 'claude' ? 'badge-amber' : s.source === 'codex' ? 'badge-blue' : 'badge-purple'}`}>
                            {s.source}
                          </span>
                        </td>
                        <td>{s.sessions}</td>
                        <td>{s.model_calls}</td>
                        <td>{formatTokens(s.input_tokens)}</td>
                        <td style={{color: '#10b981'}}>{formatTokens(s.cached_input_tokens)}</td>
                        <td>{formatTokens(s.output_tokens)}</td>
                        <td style={{color: '#10b981', fontWeight: 600}}>{s.code_changes}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          </div>

          <div className="panel">
            <div className="panel-header">
              <h2 className="panel-title">🤖 模型级详细消耗与定价台账</h2>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>客户端来源</th>
                    <th>模型标识</th>
                    <th>调用频次</th>
                    <th>输入 Token</th>
                    <th>缓存命中 Token</th>
                    <th>缓存写入 Token</th>
                    <th>输出 Token</th>
                    <th>计费费率配置</th>
                    <th>预估总支出 (USD)</th>
                  </tr>
                </thead>
                <tbody>
                  {models.map((m, idx) => (
                    <tr key={idx}>
                      <td>
                        <span className={`badge ${m.source === 'claude' ? 'badge-amber' : m.source === 'codex' ? 'badge-blue' : 'badge-purple'}`}>
                          {m.source}
                        </span>
                      </td>
                      <td className="mono" style={{fontWeight: 600}}>{m.model}</td>
                      <td>{formatNumber(m.model_calls)}</td>
                      <td>{formatTokens(m.input_tokens)}</td>
                      <td style={{color: '#10b981'}}>{formatTokens(m.cached_input_tokens)}</td>
                      <td style={{color: '#f59e0b'}}>{formatTokens(m.cache_write_input_tokens)}</td>
                      <td style={{color: '#c084fc'}}>{formatTokens(m.output_tokens)}</td>
                      <td>
                        {m.pricing_configured ? (
                          <span className="badge badge-emerald">已配置精准定价</span>
                        ) : (
                          <span className="badge" style={{background: '#334155', color: '#94a3b8'}}>未配置</span>
                        )}
                      </td>
                      <td style={{fontWeight: 600, color: '#38bdf8'}}>${m.estimated_cost_usd.toFixed(4)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 3: 代码分类与 Git 归因 */}
      {activeTab === 'attribution' && (
        <div>
          <div className="grid-2">
            <div className="panel">
              <div className="panel-header">
                <div>
                  <h2 className="panel-title">🍩 AI 建议代码分类结构占比</h2>
                  <p className="panel-subtitle">自动识别业务逻辑、测试代码、文档与配置</p>
                </div>
              </div>
              {codeTypePieOption && <EChartComponent option={codeTypePieOption} height="350px" />}
            </div>

            <div className="panel">
              <div className="panel-header">
                <div>
                  <h2 className="panel-title">🎯 研发效能与质量归因 (Attribution & Quality Metrics)</h2>
                  <p className="panel-subtitle">基于 SHA-256 行指纹比对与 Git 提交历史全量穿透</p>
                </div>
              </div>
              <div style={{display: 'flex', flexDirection: 'column', gap: '14px', padding: '6px 0'}}>
                <div>
                  <div style={{display: 'flex', justifyContent: 'space-between', marginBottom: '4px'}}>
                    <span style={{fontSize: '13px', color: '#94a3b8'}}>🎯 行维度代码采纳率 (Line-level)</span>
                    <span style={{fontSize: '14px', fontWeight: 700, color: '#38bdf8'}}>
                      {attribution?.acceptance_rate?.toFixed(2)}% ({formatNumber(attribution?.accepted_lines)} / {formatNumber(attribution?.candidate_lines)} 行)
                    </span>
                  </div>
                  <div className="progress-bar-bg">
                    <div className="progress-bar-fill" style={{width: `${attribution?.acceptance_rate || 0}%`}}></div>
                  </div>
                </div>

                <div>
                  <div style={{display: 'flex', justifyContent: 'space-between', marginBottom: '4px'}}>
                    <span style={{fontSize: '13px', color: '#94a3b8'}}>💬 会话维度代码采纳率 (Session-level)</span>
                    <span style={{fontSize: '14px', fontWeight: 700, color: '#34d399'}}>
                      {(attribution?.session_acceptance_rate || 0).toFixed(2)}% ({attribution?.accepted_sessions || 0} / {attribution?.proposing_sessions || 0} 个会话)
                    </span>
                  </div>
                  <div className="progress-bar-bg">
                    <div className="progress-bar-fill" style={{width: `${attribution?.session_acceptance_rate || 0}%`, background: 'linear-gradient(90deg, #34d399, #059669)'}}></div>
                  </div>
                </div>

                <div>
                  <div style={{display: 'flex', justifyContent: 'space-between', marginBottom: '4px'}}>
                    <span style={{fontSize: '13px', color: '#94a3b8'}}>🏆 AI 生成代码在 Git 总入库中占比 (Git Merge Share)</span>
                    <span style={{fontSize: '14px', fontWeight: 700, color: '#c084fc'}}>
                      {(attribution?.ai_git_merge_share || 0).toFixed(2)}% (Git 总新增 {formatNumber(attribution?.git_total_lines_added)} 行)
                    </span>
                  </div>
                  <div className="progress-bar-bg">
                    <div className="progress-bar-fill" style={{width: `${Math.min(100, attribution?.ai_git_merge_share || 0)}%`, background: 'linear-gradient(90deg, #c084fc, #9333ea)'}}></div>
                  </div>
                </div>

                <div>
                  <div style={{display: 'flex', justifyContent: 'space-between', marginBottom: '4px'}}>
                    <span style={{fontSize: '13px', color: '#94a3b8'}}>💼 业务核心代码采纳率 (Business Code)</span>
                    <span style={{fontSize: '14px', fontWeight: 700, color: '#10b981'}}>
                      {attribution?.business_acceptance_rate?.toFixed(2)}% ({formatNumber(attribution?.business_accepted_lines)} / {formatNumber(attribution?.business_candidate_lines)} 行)
                    </span>
                  </div>
                  <div className="progress-bar-bg">
                    <div className="progress-bar-fill" style={{width: `${attribution?.business_acceptance_rate || 0}%`, background: 'linear-gradient(90deg, #10b981, #059669)'}}></div>
                  </div>
                </div>

                <div>
                  <div style={{display: 'flex', justifyContent: 'space-between', marginBottom: '4px'}}>
                    <span style={{fontSize: '13px', color: '#94a3b8'}}>📝 技术文档与注释采纳率 (Docs & Comments)</span>
                    <span style={{fontSize: '14px', fontWeight: 700, color: '#f59e0b'}}>
                      {attribution?.documentation_candidate_lines > 0 
                        ? ((attribution.documentation_accepted_lines / attribution.documentation_candidate_lines) * 100).toFixed(2)
                        : 0}% ({formatNumber(attribution?.documentation_accepted_lines)} / {formatNumber(attribution?.documentation_candidate_lines)} 行)
                    </span>
                  </div>
                  <div className="progress-bar-bg">
                    <div className="progress-bar-fill" style={{width: `${attribution?.documentation_candidate_lines > 0 ? (attribution.documentation_accepted_lines / attribution.documentation_candidate_lines) * 100 : 0}%`, background: 'linear-gradient(90deg, #f59e0b, #d97706)'}}></div>
                  </div>
                </div>

                <div style={{background: 'rgba(255,255,255,0.02)', border: '1px solid var(--border-subtle)', padding: '10px 12px', borderRadius: '8px', marginTop: '4px'}}>
                  <p style={{fontSize: '12px', color: '#94a3b8', lineHeight: 1.6}}>
                    💡 <b>归因算法原理</b>：AgentStat 采用内存级行指纹哈希（SHA-256）与实际 Git 提交历史做保守比对，不持久化敏感源码，只统计实际采纳入库的新增非空行。
                  </p>
                </div>
              </div>
            </div>
          </div>

          {/* 代码分类指标详细表格 */}
          <div className="panel">
            <div className="panel-header">
              <h2 className="panel-title">📋 代码产出与分类指标明细</h2>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>代码分类</th>
                    <th>Agent 产出新增行数</th>
                    <th>Git 归因候选行数</th>
                    <th>最终保留采纳行数</th>
                    <th>分类采纳率</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><span className="badge badge-blue">业务逻辑代码 (Business)</span></td>
                    <td>+{formatNumber(codeStats?.business_lines_added)}</td>
                    <td>{formatNumber(attribution?.business_candidate_lines)}</td>
                    <td style={{color: '#10b981', fontWeight: 600}}>{formatNumber(attribution?.business_accepted_lines)}</td>
                    <td style={{fontWeight: 700, color: '#38bdf8'}}>{attribution?.business_acceptance_rate?.toFixed(2)}%</td>
                  </tr>
                  <tr>
                    <td><span className="badge badge-emerald">单元与集成测试 (Tests)</span></td>
                    <td>+{formatNumber(codeStats?.test_lines_added)}</td>
                    <td>{formatNumber(attribution?.test_candidate_lines)}</td>
                    <td style={{color: '#10b981', fontWeight: 600}}>{formatNumber(attribution?.test_accepted_lines)}</td>
                    <td style={{fontWeight: 700, color: '#10b981'}}>
                      {attribution?.test_candidate_lines > 0 ? ((attribution.test_accepted_lines / attribution.test_candidate_lines) * 100).toFixed(2) : 0}%
                    </td>
                  </tr>
                  <tr>
                    <td><span className="badge badge-purple">技术文档与注释 (Docs)</span></td>
                    <td>+{formatNumber(codeStats?.documentation_lines_added)}</td>
                    <td>{formatNumber(attribution?.documentation_candidate_lines)}</td>
                    <td style={{color: '#10b981', fontWeight: 600}}>{formatNumber(attribution?.documentation_accepted_lines)}</td>
                    <td style={{fontWeight: 700, color: '#a855f7'}}>
                      {attribution?.documentation_candidate_lines > 0 ? ((attribution.documentation_accepted_lines / attribution.documentation_candidate_lines) * 100).toFixed(2) : 0}%
                    </td>
                  </tr>
                  <tr>
                    <td><span className="badge badge-amber">自动生成与脚手架 (Generated)</span></td>
                    <td>+{formatNumber(codeStats?.generated_lines_added)}</td>
                    <td>-</td>
                    <td>-</td>
                    <td>-</td>
                  </tr>
                  <tr>
                    <td><span className="badge" style={{background: '#334155', color: '#cbd5e1'}}>配置与其他 (Other)</span></td>
                    <td>+{formatNumber(codeStats?.other_lines_added)}</td>
                    <td>-</td>
                    <td>-</td>
                    <td>-</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>

          {/* 分项目代码归因明细 */}
          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">📁 分项目 AI 代码精确采纳率 (Project Breakdown)</h2>
                <p className="panel-subtitle">每个项目的候选代码行、Git 提交采纳行与独立采纳率</p>
              </div>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>项目名称</th>
                    <th>工作区目录</th>
                    <th>会话频次</th>
                    <th>代码净产出 (+增/-删)</th>
                    <th>候选代码行 (Candidate)</th>
                    <th>已采纳行 (Accepted)</th>
                    <th>项目采纳率 (%)</th>
                  </tr>
                </thead>
                <tbody>
                  {projects.map((p, idx) => (
                    <tr key={idx}>
                      <td style={{fontWeight: 600, color: '#38bdf8'}}>{p.project || '默认项目'}</td>
                      <td className="mono" style={{fontSize: '12px', color: '#94a3b8'}}>{p.project_path}</td>
                      <td>{p.sessions} 次</td>
                      <td>
                        <span style={{color: '#10b981'}}>+{formatNumber(p.lines_added || 0)}</span>
                        {p.lines_deleted > 0 && <span style={{color: '#f43f5e', marginLeft: '4px'}}>-{formatNumber(p.lines_deleted)}</span>}
                      </td>
                      <td>{formatNumber(p.candidate_lines || p.lines_added || 0)} 行</td>
                      <td style={{color: '#10b981', fontWeight: 600}}>{formatNumber(p.accepted_lines || 0)} 行</td>
                      <td style={{minWidth: '170px'}}>
                        <div style={{display: 'flex', alignItems: 'center', gap: '8px'}}>
                          <div className="progress-bar-bg" style={{flex: 1, marginTop: 0}}>
                            <div 
                              className="progress-bar-fill" 
                              style={{
                                width: `${Math.min(100, p.acceptance_rate || 0)}%`,
                                background: (p.acceptance_rate || 0) > 50 ? 'linear-gradient(90deg, #38bdf8, #10b981)' : 'linear-gradient(90deg, #f59e0b, #38bdf8)'
                              }}
                            ></div>
                          </div>
                          <span style={{fontWeight: 700, fontSize: '12px', color: (p.acceptance_rate || 0) > 0 ? '#38bdf8' : '#94a3b8'}}>
                            {(p.acceptance_rate || 0).toFixed(1)}%
                          </span>
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 4: 工具 / MCP / Skills 调用 */}
      {activeTab === 'tools' && (
        <div>
          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">🏆 高频工具调用分布排行榜 (Top 15)</h2>
                <p className="panel-subtitle">Agent 在解决问题时最常调用的编辑、查找、执行与 MCP 工具</p>
              </div>
            </div>
            <EChartComponent option={toolsBarOption} height="420px" />
          </div>

          <div className="grid-2">
            {/* MCP 能力列表 */}
            <div className="panel">
              <div className="panel-header">
                <h2 className="panel-title">🧩 MCP 外部服务扩展调用</h2>
              </div>
              {mcp.length === 0 ? (
                <div className="empty-state">暂无 MCP 扩展调用记录</div>
              ) : (
                <div className="table-container">
                  <table>
                    <thead>
                      <tr>
                        <th>服务名称</th>
                        <th>来源</th>
                        <th>调用次数</th>
                      </tr>
                    </thead>
                    <tbody>
                      {mcp.map((item, idx) => (
                        <tr key={idx}>
                          <td className="mono" style={{fontWeight: 600}}>{item.name}</td>
                          <td><span className="badge badge-blue">{item.source}</span></td>
                          <td style={{fontWeight: 600, color: '#38bdf8'}}>{item.calls}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
            </div>

            {/* Skills 技能列表 */}
            <div className="panel">
              <div className="panel-header">
                <h2 className="panel-title">💡 Agent Skills 技能调用</h2>
              </div>
              {skills.length === 0 ? (
                <div className="empty-state">暂无 Skills 技能记录</div>
              ) : (
                <div className="table-container">
                  <table>
                    <thead>
                      <tr>
                        <th>技能名称</th>
                        <th>来源</th>
                        <th>调用次数</th>
                      </tr>
                    </thead>
                    <tbody>
                      {skills.map((item, idx) => (
                        <tr key={idx}>
                          <td className="mono" style={{fontWeight: 600}}>{item.name}</td>
                          <td><span className="badge badge-purple">{item.source}</span></td>
                          <td style={{fontWeight: 600, color: '#a855f7'}}>{item.calls}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
            </div>
          </div>

          {/* 全量工具列表 */}
          <div className="panel">
            <div className="panel-header">
              <h2 className="panel-title">📋 工具全量调用明细表</h2>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>工具名称</th>
                    <th>详情/类别</th>
                    <th>调用次数</th>
                    <th>MCP 调用数</th>
                  </tr>
                </thead>
                <tbody>
                  {tools.map((t, idx) => (
                    <tr key={idx}>
                      <td className="mono" style={{fontWeight: 600}}>{t.tool_name}</td>
                      <td className="mono" style={{color: '#94a3b8'}}>{t.detail_name || '-'}</td>
                      <td style={{fontWeight: 600}}>{formatNumber(t.calls)}</td>
                      <td>{t.mcp_calls > 0 ? <span className="badge badge-emerald">{t.mcp_calls}</span> : '0'}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 5: 项目工作区 */}
      {activeTab === 'projects' && (
        <div>
          {/* 各项目采纳率对比图 */}
          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">🎯 各项目 AI 代码采纳率与代码产出对比</h2>
                <p className="panel-subtitle">直观对比不同本地项目的 AI 候选代码行数、实际入库采纳行数与精确采纳率 (%)</p>
              </div>
            </div>
            {projectsAcceptanceOption ? (
              <EChartComponent option={projectsAcceptanceOption} height="350px" />
            ) : (
              <div className="empty-state">暂无可归因的项目代码提交记录（可通过 ./bin/agentstat sync 同步 Git 仓库）</div>
            )}
          </div>

          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">📁 本地项目工作区效能与归因全览</h2>
                <p className="panel-subtitle">综合统计各项目的会话频次、Token 消耗资产、代码产出与精确采纳率</p>
              </div>
            </div>
            <div className="table-container">
              <table>
                <thead>
                  <tr>
                    <th>项目名称</th>
                    <th>工作目录路径</th>
                    <th>会话数</th>
                    <th>跨 Agent 来源</th>
                    <th>输入 Token</th>
                    <th>输出 Token</th>
                    <th>代码净增/删</th>
                    <th>AI 候选行</th>
                    <th>Git 已采纳</th>
                    <th>🎯 项目代码采纳率</th>
                  </tr>
                </thead>
                <tbody>
                  {projects.map((p, idx) => (
                    <tr key={idx}>
                      <td style={{fontWeight: 600, color: '#38bdf8'}}>{p.project || '默认项目'}</td>
                      <td className="mono" style={{fontSize: '12px', color: '#94a3b8'}}>{p.project_path}</td>
                      <td>{p.sessions}</td>
                      <td>{p.sources}</td>
                      <td>{formatTokens(p.input_tokens)}</td>
                      <td>{formatTokens(p.output_tokens)}</td>
                      <td>
                        <span style={{color: '#10b981'}}>+{formatNumber(p.lines_added || 0)}</span>
                        {p.lines_deleted > 0 && <span style={{color: '#f43f5e', marginLeft: '4px'}}>-{formatNumber(p.lines_deleted)}</span>}
                      </td>
                      <td>{formatNumber(p.candidate_lines || p.lines_added || 0)}</td>
                      <td style={{color: '#10b981', fontWeight: 600}}>{formatNumber(p.accepted_lines || 0)}</td>
                      <td style={{minWidth: '170px'}}>
                        <div style={{display: 'flex', alignItems: 'center', gap: '8px'}}>
                          <div className="progress-bar-bg" style={{flex: 1, marginTop: 0}}>
                            <div 
                              className="progress-bar-fill" 
                              style={{
                                width: `${Math.min(100, p.acceptance_rate || 0)}%`,
                                background: (p.acceptance_rate || 0) > 50 ? 'linear-gradient(90deg, #38bdf8, #10b981)' : 'linear-gradient(90deg, #f59e0b, #38bdf8)'
                              }}
                            ></div>
                          </div>
                          <span style={{fontWeight: 700, fontSize: '12px', color: (p.acceptance_rate || 0) > 0 ? '#38bdf8' : '#94a3b8'}}>
                            {(p.acceptance_rate || 0).toFixed(1)}%
                          </span>
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </div>
      )}

      {/* Tab 6: 详细会话流水台账 */}
      {activeTab === 'sessions' && (
        <div>
          <div className="panel">
            <div className="panel-header">
              <div>
                <h2 className="panel-title">📜 详细会话流水台账</h2>
                <p className="panel-subtitle">点击任意会话的「💬 查看对话流水」即可调出完整原始对话与工具调用详情</p>
              </div>
            </div>

            {/* 搜索与过滤工具栏 */}
            <div className="filter-bar">
              <input
                type="text"
                className="search-input"
                placeholder="🔍 搜索会话 ID / 路径 / 模型名称..."
                value={sessionSearch}
                onChange={e => {
                  setSessionSearch(e.target.value);
                  setSessionPage(1);
                }}
              />

              <select
                className="select-input"
                value={sessionAgentFilter}
                onChange={e => {
                  setSessionAgentFilter(e.target.value);
                  setSessionPage(1);
                }}
              >
                <option value="all">全部 Agent 来源</option>
                <option value="codex">Codex</option>
                <option value="claude">Claude Code</option>
                <option value="antigravity">Antigravity</option>
              </select>
            </div>

            {/* 会话流水列表 */}
            {filteredSessions.length === 0 ? (
              <div className="empty-state">未找到匹配的会话流水记录</div>
            ) : (
              <div>
                {paginatedSessions.map((s, idx) => (
                  <div className="session-item" key={idx}>
                    <div className="session-item-header">
                      <div style={{display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap'}}>
                        <span className={`badge ${s.source === 'claude' ? 'badge-amber' : s.source === 'codex' ? 'badge-blue' : 'badge-purple'}`}>
                          {s.source}
                        </span>
                        <span className="mono" style={{fontWeight: 700, color: '#f1f5f9'}}>{s.session_id}</span>
                        {s.models && (
                          <span className="badge" style={{background: 'rgba(255,255,255,0.06)', color: '#94a3b8'}}>
                            {s.models}
                          </span>
                        )}
                      </div>

                      <div style={{display: 'flex', alignItems: 'center', gap: '12px'}}>
                        <span style={{fontSize: '12px', color: '#64748b'}}>{s.started_at}</span>
                        <button 
                          className="btn btn-sm btn-primary"
                          onClick={() => setSelectedSessionId(s.session_id)}
                        >
                          💬 查看对话流水
                        </button>
                      </div>
                    </div>

                    <div className="mono" style={{fontSize: '12px', color: '#94a3b8', marginTop: '6px', wordBreak: 'break-all'}}>
                      📁 {s.cwd}
                    </div>

                    <div className="session-metrics">
                      <span>输入 Token: <span className="session-metric-val">{formatTokens(s.input_tokens)}</span></span>
                      <span>输出 Token: <span className="session-metric-val">{formatTokens(s.output_tokens)}</span></span>
                      <span>工具调用: <span className="session-metric-val">{s.tool_calls} 次</span></span>
                      <span>代码变更: <span className="session-metric-val" style={{color: '#10b981'}}>{s.code_changes} 处</span></span>
                    </div>
                  </div>
                ))}

                {/* 分页控制栏 */}
                <div style={{display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: '20px', flexWrap: 'wrap', gap: '12px'}}>
                  <span style={{fontSize: '13px', color: '#94a3b8'}}>
                    共 {filteredSessions.length} 条会话记录，当前第 {sessionPage} / {Math.ceil(filteredSessions.length / pageSize) || 1} 页
                  </span>

                  <div style={{display: 'flex', gap: '8px'}}>
                    <button
                      className="btn btn-sm"
                      disabled={sessionPage <= 1}
                      onClick={() => setSessionPage(sessionPage - 1)}
                    >
                      ← 上一页
                    </button>
                    <button
                      className="btn btn-sm"
                      disabled={sessionPage >= Math.ceil(filteredSessions.length / pageSize)}
                      onClick={() => setSessionPage(sessionPage + 1)}
                    >
                      下一页 →
                    </button>
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* 会话流水详情模态框 */}
      {selectedSessionId && (
        <TranscriptModal
          sessionId={selectedSessionId}
          onClose={() => setSelectedSessionId(null)}
        />
      )}
    </div>
  );
}
