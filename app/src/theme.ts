// Tokens de diseno: una sola fuente de verdad para colores, espaciado y texto.
// El rojo se reserva EXCLUSIVAMENTE para alarmas de gas.

export const colors = {
  background: '#F1F5F9',
  surface: '#FFFFFF',
  surfaceSoft: '#F8FAFC',
  border: '#E2E8F0',

  text: '#0F172A',
  textSecondary: '#475569',
  textMuted: '#64748B',

  primary: '#0F766E',
  primaryDark: '#115E59',
  primarySoft: '#CCFBF1',

  success: '#15803D',
  successSoft: '#DCFCE7',

  warning: '#B45309',
  warningSoft: '#FEF3C7',

  danger: '#B91C1C',
  dangerSoft: '#FEE2E2',

  route: '#0EA5E9',
  grid: '#E2E8F0',
} as const;

export const space = {
  xs: 4,
  sm: 8,
  md: 12,
  lg: 16,
  xl: 24,
  xxl: 32,
} as const;

export const radius = {
  sm: 8,
  md: 12,
  lg: 16,
  pill: 999,
} as const;

export const font = {
  xs: 11,
  sm: 12,
  md: 14,
  lg: 16,
  xl: 20,
  xxl: 28,
} as const;
