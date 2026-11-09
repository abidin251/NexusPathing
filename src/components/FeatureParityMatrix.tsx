import React, { useState } from 'react';
import { FEATURE_PARITY_MATRIX } from '../data/samplePackData';
import { FeatureParityItem } from '../types/pathing';
import { CheckCircle2, Search } from 'lucide-react';

export const FeatureParityMatrix: React.FC = () => {
  const [searchQuery, setSearchQuery] = useState<string>('');
  const [selectedCategory, setSelectedCategory] = useState<string>('All');

  const categories = [
    'All',
    'Pack Formats',
    'Rendering & Shaders',
    'Behaviors & Filters',
    'Blish Exclusive Attributes',
    'Lua Scripting Engine',
  ];

  const filteredItems = FEATURE_PARITY_MATRIX.filter((item: FeatureParityItem) => {
    const matchesCategory = selectedCategory === 'All' || item.category === selectedCategory;
    const matchesQuery =
      item.featureName.toLowerCase().includes(searchQuery.toLowerCase()) ||
      item.blishSourceFile.toLowerCase().includes(searchQuery.toLowerCase()) ||
      item.nexusCppFile.toLowerCase().includes(searchQuery.toLowerCase()) ||
      item.description.toLowerCase().includes(searchQuery.toLowerCase());
    return matchesCategory && matchesQuery;
  });

  return (
    <div className="space-y-4">
      <div className="bg-[#111622] border border-slate-800 rounded-lg p-4 flex flex-wrap items-center justify-between gap-4">
        <div className="flex flex-wrap items-center gap-1 p-1 bg-slate-900 border border-slate-800 rounded-md">
          {categories.map((cat) => (
            <button
              key={cat}
              onClick={() => setSelectedCategory(cat)}
              className={`px-3 py-1.5 text-xs font-medium rounded transition-colors whitespace-nowrap ${
                selectedCategory === cat
                  ? 'bg-sky-500 text-slate-950 font-semibold'
                  : 'text-slate-300 hover:text-white'
              }`}
            >
              {cat}
            </button>
          ))}
        </div>

        <div className="relative w-full sm:w-72">
          <Search className="w-3.5 h-3.5 text-slate-400 absolute left-3 top-1/2 -translate-y-1/2" />
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Filter Blish C# or Nexus C++ modules..."
            className="w-full pl-8 pr-3 py-1.5 bg-slate-900 border border-slate-800 rounded-md text-xs text-slate-200 placeholder-slate-500 focus:outline-none focus:border-sky-500"
          />
        </div>
      </div>

      <div className="bg-[#111622] border border-slate-800 rounded-lg overflow-hidden">
        <div className="overflow-x-auto">
          <table className="w-full text-left border-collapse text-xs">
            <thead>
              <tr className="border-b border-slate-800 bg-slate-900/80 text-slate-400 font-medium">
                <th className="py-3 px-4">Blish HUD Pathing Feature</th>
                <th className="py-3 px-4">Blish C# Source</th>
                <th className="py-3 px-4">Raidcore Nexus C++20 Port</th>
                <th className="py-3 px-4">Technical Implementation Mechanism</th>
                <th className="py-3 px-4 text-right">Port Status</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-800/70">
              {filteredItems.map((item) => (
                <tr key={item.id} className="hover:bg-slate-900/50 transition-colors">
                  <td className="py-3 px-4 align-top">
                    <div className="font-semibold text-slate-100">{item.featureName}</div>
                    <div className="text-slate-400 text-[11px] mt-0.5">
                      {item.category} · {item.description}
                    </div>
                  </td>
                  <td className="py-3 px-4 align-top font-mono text-slate-400 whitespace-nowrap">
                    {item.blishSourceFile}
                  </td>
                  <td className="py-3 px-4 align-top font-mono text-sky-400 whitespace-nowrap">
                    {item.nexusCppFile}
                  </td>
                  <td className="py-3 px-4 align-top text-slate-300 max-w-md">
                    {item.technicalMechanism}
                  </td>
                  <td className="py-3 px-4 align-top text-right whitespace-nowrap">
                    <span className="inline-flex items-center gap-1.5 text-emerald-400 font-medium">
                      <CheckCircle2 className="w-3.5 h-3.5 shrink-0" />
                      <span>{item.status}</span>
                    </span>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
};
