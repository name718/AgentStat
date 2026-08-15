import React, { useRef, useEffect } from 'react';
import * as echarts from 'echarts';

export default function EChartComponent({ option, style, height = '350px' }) {
  const chartRef = useRef(null);
  const instanceRef = useRef(null);

  useEffect(() => {
    if (!chartRef.current) return;

    if (!instanceRef.current) {
      instanceRef.current = echarts.getInstanceByDom(chartRef.current) || echarts.init(chartRef.current, null, {
        renderer: 'canvas'
      });
    }

    if (option && instanceRef.current) {
      instanceRef.current.setOption(option, true);
    }

    let resizeObserver = null;
    if (window.ResizeObserver && chartRef.current) {
      resizeObserver = new ResizeObserver(() => {
        if (instanceRef.current) {
          try { instanceRef.current.resize(); } catch (_) {}
        }
      });
      resizeObserver.observe(chartRef.current);
    }

    const handleWindowResize = () => {
      if (instanceRef.current) {
        try { instanceRef.current.resize(); } catch (_) {}
      }
    };
    window.addEventListener('resize', handleWindowResize);

    const t1 = setTimeout(() => handleWindowResize(), 50);
    const t2 = setTimeout(() => handleWindowResize(), 300);

    return () => {
      clearTimeout(t1);
      clearTimeout(t2);
      window.removeEventListener('resize', handleWindowResize);
      if (resizeObserver) {
        try { resizeObserver.disconnect(); } catch (_) {}
      }
      if (instanceRef.current) {
        try { instanceRef.current.dispose(); } catch (_) {}
        instanceRef.current = null;
      }
    };
  }, [option]);

  return <div ref={chartRef} style={{ width: '100%', minHeight: height, height, ...style }} />;
}
