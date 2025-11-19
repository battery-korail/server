"use client";

import { Line } from "react-chartjs-2";
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
} from "chart.js";

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend);

interface DPChartProps {
  labels: string[];
  data: number[];
}

export default function DPChart({ labels, data }: DPChartProps) {
  const chartData = {
    labels,
    datasets: [
      {
        label: "dP (Pa)",
        data,
        borderColor: "rgb(75, 192, 192)",
        backgroundColor: "rgba(75, 192, 192, 0.2)",
        tension: 0.3,
      },
    ],
  };

  return <Line
  data={chartData}
  options={{
    responsive: true,
    animation: { duration: 300 },
    scales: { y: { beginAtZero: false } },
  }}
 />;
}
