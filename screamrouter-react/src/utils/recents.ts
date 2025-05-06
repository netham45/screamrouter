export const addToRecents = (type: 'sources' | 'sinks' | 'routes', name: string) => {
  const key = `recent${type.charAt(0).toUpperCase() + type.slice(1)}`;
  let recents = JSON.parse(localStorage.getItem(key) || "[]");
  
  // Remove duplicates
  recents = recents.filter((item: string) => item !== name);
  
  // Add to front and limit to 5 items
  recents.unshift(name);
  if (recents.length > 5) recents.pop();
  
  localStorage.setItem(key, JSON.stringify(recents));
};

export const getRecents = (type: 'sources' | 'sinks' | 'routes'): string[] => {
  return JSON.parse(localStorage.getItem(`recent${type.charAt(0).toUpperCase() + type.slice(1)}`) || "[]");
};
