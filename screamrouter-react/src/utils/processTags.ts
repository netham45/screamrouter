export const canonicalizeProcessTag = (tag?: string | null): string | null => {
  if (!tag) {
    return null;
  }
  const cleaned = tag.replace(/\u0000/g, '');
  const withoutSuffix = cleaned.replace(/#[0-9a-fA-F]+$/, '');
  const withoutWildcard = withoutSuffix.replace(/\*+$/, '');
  const compact = withoutWildcard.trim();
  if (!compact) {
    return null;
  }
  return `${compact}*`;
};

export const formatProcessTag = (tag?: string | null): string => {
  const canonical = canonicalizeProcessTag(tag);
  if (canonical) {
    return canonical;
  }
  return tag ?? '—';
};
