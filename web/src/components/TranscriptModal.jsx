import React, { useState, useEffect, useMemo } from 'react';

// 严格提取并扁平化任意对象/数组中的文本内容，防止 React Error #31
export function extractString(val) {
  if (val === null || val === undefined) return '';
  if (typeof val === 'string') return val;
  if (typeof val === 'number' || typeof val === 'boolean') return String(val);
  if (Array.isArray(val)) {
    return val.map(item => extractString(item)).filter(Boolean).join('\n');
  }
  if (typeof val === 'object') {
    if (typeof val.text === 'string') return val.text;
    if (val.text && typeof val.text === 'object') return extractString(val.text);
    if (val.content !== undefined) return extractString(val.content);
    if (val.prompt !== undefined) return extractString(val.prompt);
    if (val.message !== undefined) return extractString(val.message);
    if (typeof val.thinking === 'string') return `[Thinking: ${val.thinking}]`;
    if (typeof val.output === 'string') return val.output;
    if (typeof val.result === 'string') return val.result;
    try {
      return JSON.stringify(val, null, 2);
    } catch (_) {
      return String(val);
    }
  }
  return String(val);
}

function cleanUserPrompt(str) {
  if (typeof str !== 'string') return extractString(str);
  const match = str.match(/<USER_REQUEST>([\s\S]*?)<\/USER_REQUEST>/);
  if (match && match[1]) {
    return match[1].trim();
  }
  return str.trim();
}

export default function TranscriptModal({ sessionId, onClose }) {
  const [loading, setLoading] = useState(true);
  const [transcriptData, setTranscriptData] = useState(null);
  const [filterType, setFilterType] = useState('all');
  const [searchKeyword, setSearchKeyword] = useState('');
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    if (!sessionId) return;
    setLoading(true);
    fetch(`/api/transcript?id=${encodeURIComponent(sessionId)}`)
      .then(r => r.json())
      .then(data => {
        setTranscriptData(data);
      })
      .catch(err => {
        console.error("加载会话对话流失败", err);
      })
      .finally(() => {
        setLoading(false);
      });
  }, [sessionId]);

  const handleCopyAll = () => {
    if (!transcriptData) return;
    navigator.clipboard.writeText(JSON.stringify(transcriptData, null, 2));
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const parsedMessages = useMemo(() => {
    if (!transcriptData || !Array.isArray(transcriptData.messages)) return [];

    const list = [];
    transcriptData.messages.forEach((m, idx) => {
      if (!m || typeof m !== 'object') return;

      // 1. Antigravity 事件规范
      if (m.type === 'USER_INPUT') {
        list.push({
          id: idx,
          role: 'user',
          timestamp: m.timestamp ? new Date(m.timestamp).toLocaleTimeString() : '',
          content: cleanUserPrompt(m.content || (m.tool_calls && JSON.stringify(m.tool_calls)) || '')
        });
      } else if (m.type === 'PLANNER_RESPONSE' || m.type === 'MODEL_RESPONSE') {
        list.push({
          id: idx,
          role: 'assistant',
          timestamp: m.timestamp ? new Date(m.timestamp).toLocaleTimeString() : '',
          content: m.content || '',
          tool_calls: m.tool_calls || []
        });
      } else if (m.type === 'CHECKPOINT' || m.source === 'CHECKPOINT') {
        list.push({
          id: idx,
          role: 'checkpoint',
          timestamp: m.timestamp ? new Date(m.timestamp).toLocaleTimeString() : '',
          content: m.content || ''
        });
      }
      // 2. Claude Code 规范
      else if (m.type === 'user') {
        list.push({
          id: idx,
          role: 'user',
          timestamp: m.timestamp ? new Date(m.timestamp).toLocaleTimeString() : '',
          content: cleanUserPrompt(m.message?.content || m.content || '')
        });
      } else if (m.type === 'assistant') {
        const content = m.message?.content || m.content || [];
        let textContent = '';
        let tools = [];

        if (typeof content === 'string') {
          textContent = content;
        } else if (Array.isArray(content)) {
          content.forEach(c => {
            if (c.type === 'text') textContent += (c.text || '') + '\n';
            else if (c.type === 'thinking') textContent += `[Thinking: ${c.thinking || ''}]\n`;
            else if (c.type === 'tool_use') tools.push({ name: c.name, args: c.input });
          });
        }

        list.push({
          id: idx,
          role: 'assistant',
          timestamp: m.timestamp ? new Date(m.timestamp).toLocaleTimeString() : '',
          content: textContent.trim(),
          tool_calls: tools
        });
      }
      // 3. 通用/Codex 规范
      else if (m.role === 'user' || m.role === 'human') {
        list.push({
          id: idx,
          role: 'user',
          timestamp: m.timestamp || '',
          content: cleanUserPrompt(m.content || m.text || '')
        });
      } else if (m.role === 'assistant' || m.role === 'bot') {
        list.push({
          id: idx,
          role: 'assistant',
          timestamp: m.timestamp || '',
          content: m.content || m.text || '',
          tool_calls: m.tool_calls || m.tools || []
        });
      }
    });

    return list.filter(item => {
      if (filterType === 'user' && item.role !== 'user') return false;
      if (filterType === 'assistant' && item.role !== 'assistant') return false;
      if (filterType === 'tools' && (!item.tool_calls || item.tool_calls.length === 0)) return false;

      if (searchKeyword.trim()) {
        const kw = searchKeyword.toLowerCase();
        const textMatch = typeof item.content === 'string' && item.content.toLowerCase().includes(kw);
        const toolMatch = item.tool_calls && JSON.stringify(item.tool_calls).toLowerCase().includes(kw);
        return textMatch || toolMatch;
      }
      return true;
    });
  }, [transcriptData, filterType, searchKeyword]);

  if (!sessionId) return null;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content" onClick={e => e.stopPropagation()}>
        <div className="modal-header">
          <div>
            <div className="modal-title">
              <span>💬 真实会话对话流水详情</span>
              {transcriptData && (
                <span className={`badge ${transcriptData.source === 'claude' ? 'badge-amber' : transcriptData.source === 'codex' ? 'badge-blue' : 'badge-purple'}`}>
                  {transcriptData.source}
                </span>
              )}
            </div>
            <div className="modal-subinfo mono">
              会话 ID: <b>{sessionId}</b> | 启动时间: {transcriptData?.started_at || '-'}
            </div>
            {transcriptData?.cwd && (
              <div className="modal-subinfo mono" style={{color: '#64748b'}}>
                📁 工作目录: {transcriptData.cwd}
              </div>
            )}
          </div>

          <div style={{display: 'flex', alignItems: 'center', gap: '8px'}}>
            <button className="btn btn-sm" onClick={handleCopyAll}>
              {copied ? '✓ 已复制 JSON' : '📋 复制完整流水'}
            </button>
            <button className="modal-close-btn" onClick={onClose}>✕</button>
          </div>
        </div>

        <div className="modal-filter-bar">
          <div className="segmented-control">
            <button 
              className={`segmented-item ${filterType === 'all' ? 'active' : ''}`}
              onClick={() => setFilterType('all')}
            >
              全部消息 ({parsedMessages.length})
            </button>
            <button 
              className={`segmented-item ${filterType === 'user' ? 'active' : ''}`}
              onClick={() => setFilterType('user')}
            >
              仅提问 (User)
            </button>
            <button 
              className={`segmented-item ${filterType === 'assistant' ? 'active' : ''}`}
              onClick={() => setFilterType('assistant')}
            >
              仅回答 (AI)
            </button>
            <button 
              className={`segmented-item ${filterType === 'tools' ? 'active' : ''}`}
              onClick={() => setFilterType('tools')}
            >
              仅工具调用 (Tools)
            </button>
          </div>

          <input
            type="text"
            className="search-input"
            style={{maxWidth: '320px'}}
            placeholder="🔍 在本次对话内容中搜索..."
            value={searchKeyword}
            onChange={e => setSearchKeyword(e.target.value)}
          />
        </div>

        <div className="chat-container">
          {loading ? (
            <div className="empty-state">正在从本地磁盘读取真实会话流水...</div>
          ) : parsedMessages.length === 0 ? (
            <div className="empty-state">未找到匹配的对话消息</div>
          ) : (
            parsedMessages.map((msg) => {
              if (msg.role === 'checkpoint') {
                return (
                  <div className="checkpoint-divider" key={msg.id}>
                    📌 上下文截断与检查点恢复
                  </div>
                );
              }

              if (msg.role === 'user') {
                return (
                  <div className="chat-bubble chat-bubble-user" key={msg.id}>
                    <div className="chat-bubble-header">
                      <span>👤 开发者提问</span>
                      <span>{msg.timestamp}</span>
                    </div>
                    <div className="chat-body-user">
                      {extractString(msg.content) || '(无提问文本)'}
                    </div>
                  </div>
                );
              }

              if (msg.role === 'assistant') {
                return (
                  <div className="chat-bubble chat-bubble-assistant" key={msg.id}>
                    <div className="chat-bubble-header">
                      <span>🤖 AI Agent 响应</span>
                      <span>{msg.timestamp}</span>
                    </div>
                    <div className="chat-body-assistant">
                      {extractString(msg.content) && <div style={{whiteSpace: 'pre-wrap'}}>{extractString(msg.content)}</div>}
                      
                      {msg.tool_calls && msg.tool_calls.length > 0 && (
                        <div style={{marginTop: '10px'}}>
                          {msg.tool_calls.map((tool, tIdx) => (
                            <div className="tool-card" key={tIdx}>
                              <div className="tool-card-title">
                                <span>🛠️ 调用的工具：</span>
                                <code>{tool.name || tool.tool_name || 'Tool Call'}</code>
                              </div>
                              {tool.args && (
                                <pre className="tool-card-code mono">
                                  {extractString(tool.args)}
                                </pre>
                              )}
                            </div>
                          ))}
                        </div>
                      )}
                    </div>
                  </div>
                );
              }
              return null;
            })
          )}
        </div>
      </div>
    </div>
  );
}
