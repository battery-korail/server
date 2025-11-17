"use client";

import { useEffect, useState } from "react";
import DPChart from "./components/DPChart";
import axios from "axios";

interface SavedValue {
  id: number;
  dp_pa: number;
  created_at: string;
}

export default function Home() {
  const [labels, setLabels] = useState<string[]>([]);
  const [data, setData] = useState<number[]>([]);
  const [currentValue, setCurrentValue] = useState<number>(0);

  const [savedValues, setSavedValues] = useState<SavedValue[]>([]);
  const [page, setPage] = useState(1);
  const [totalPages, setTotalPages] = useState(1);
  const API_URL = process.env.NEXT_PUBLIC_API_URL;


  const fetchRealTime = async () => {
    try {
      const res = await axios.get(`${API_URL}/dp?limit=30`);
      const logs = res.data as any[];
      setLabels(logs.map((l: any) => new Date(l.created_at).toLocaleTimeString()));
      const dpVals = logs.map((l: any) => parseFloat(l.dp_pa));
      setData(dpVals);
      setCurrentValue(dpVals[dpVals.length - 1] || 0);
    } catch (err) {
      console.error(err);
    }
  };

  const fetchSaved = async (page: number) => {
    try {
      const res = await axios.get(`${API_URL}/dp/saved?page=${page}&per_page=5`);
      setSavedValues(res.data.data);
      setPage(res.data.page);
      setTotalPages(res.data.total_pages);
    } catch (err) {
      console.error(err);
    }
  };

  const handleSave = async () => {
    try {
      await axios.post(
       `${API_URL}/dp/save`,
        { dp_pa: currentValue },
        { headers: { "Content-Type": "application/json" } }
      );
      fetchSaved(page);
    } catch (err) {
      console.error(err);
    }
  };

  useEffect(() => {
    fetchRealTime();
    const interval = setInterval(fetchRealTime, 1000);
    fetchSaved(1);
    return () => clearInterval(interval);
  }, []);

  return (
    <div className="p-6 bg-gray-100 min-h-screen space-y-6">
      {/* 실시간 dP 카드 */}
      <div className="bg-white rounded-xl shadow-md p-6">
        <h2 className="text-2xl font-bold mb-4">실시간 dP</h2>
        <DPChart labels={labels} data={data} />
        <div className="mt-4 flex items-center justify-between">
          <span
            className="text-3xl font-extrabold text-gray-700 cursor-default"
            title="현재 값"
          >
            현재 값: {currentValue.toFixed(2)} Pa
          </span>
          <button
            className="px-5 py-2 bg-gray-200 text-gray-700 rounded-lg hover:bg-gray-300 transition cursor-pointer"
            onClick={handleSave}
          >
            저장
          </button>
        </div>
      </div>

      {/* 저장된 값 테이블 카드 */}
      <div className="bg-white rounded-xl shadow-md p-6">
        <h2 className="text-2xl font-bold mb-4">저장된 값</h2>
        <div className="overflow-x-auto">
          <table className="w-full text-left">
            <thead>
              <tr className="bg-gray-100">
                <th className="px-3 py-2 text-gray-600">ID</th>
                <th className="px-3 py-2 text-gray-600">값 (Pa)</th>
                <th className="px-3 py-2 text-gray-600">저장 시간</th>
              </tr>
            </thead>
            <tbody>
              {savedValues.map((v) => (
                <tr key={v.id} className="hover:bg-gray-50 transition">
                  <td className="px-3 py-2 border-b border-gray-200">{v.id}</td>
                  <td className="px-3 py-2 font-medium border-b border-gray-200">{v.dp_pa.toFixed(2)}</td>
                  <td className="px-3 py-2 border-b border-gray-200">{new Date(v.created_at).toLocaleString()}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        {/* 페이지네이션 */}
        <div className="mt-4 flex justify-between items-center">
          <button
            className="px-3 py-1 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 transition disabled:opacity-50 cursor-pointer"
            onClick={() => page > 1 && fetchSaved(page - 1)}
            disabled={page <= 1}
          >
            이전
          </button>
          <span className="font-semibold">
            {page} / {totalPages}
          </span>
          <button
            className="px-3 py-1 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 transition disabled:opacity-50 cursor-pointer"
            onClick={() => page < totalPages && fetchSaved(page + 1)}
            disabled={page >= totalPages}
          >
            다음
          </button>
        </div>
      </div>
    </div>
  );
}
