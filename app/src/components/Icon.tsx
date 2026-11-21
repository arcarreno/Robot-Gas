import React from 'react';
import { Circle, Line, Path, Polygon, Svg } from 'react-native-svg';
import { colors } from '../theme';

export type NombreIcono =
  | 'play'
  | 'stop'
  | 'alerta'
  | 'check'
  | 'gas'
  | 'ruta'
  | 'robot'
  | 'limpiar';

type Props = {
  nombre: NombreIcono;
  size?: number;
  color?: string;
};

/** Iconos vectoriales propios: sin emojis y sin dependencias extra. */
export function Icon({ nombre, size = 18, color = colors.primary }: Props) {
  const comun = { width: size, height: size, viewBox: '0 0 24 24' };

  switch (nombre) {
    case 'play':
      return (
        <Svg {...comun}>
          <Polygon points="7,4 20,12 7,20" fill={color} />
        </Svg>
      );
    case 'stop':
      return (
        <Svg {...comun}>
          <Path d="M6 6 H18 V18 H6 Z" fill={color} />
        </Svg>
      );
    case 'alerta':
      return (
        <Svg {...comun}>
          <Polygon
            points="12,3 22,20 2,20"
            fill="none"
            stroke={color}
            strokeWidth={2}
            strokeLinejoin="round"
          />
          <Line x1={12} y1={9} x2={12} y2={14} stroke={color} strokeWidth={2} strokeLinecap="round" />
          <Circle cx={12} cy={17} r={1.2} fill={color} />
        </Svg>
      );
    case 'check':
      return (
        <Svg {...comun}>
          <Path
            d="M4 12 L10 18 L20 6"
            fill="none"
            stroke={color}
            strokeWidth={2.5}
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </Svg>
      );
    case 'gas':
      return (
        <Svg {...comun}>
          <Circle cx={12} cy={12} r={9} fill="none" stroke={color} strokeWidth={2} />
          <Circle cx={12} cy={12} r={3.5} fill={color} />
        </Svg>
      );
    case 'ruta':
      return (
        <Svg {...comun}>
          <Path
            d="M4 18 L10 10 L15 14 L20 5"
            fill="none"
            stroke={color}
            strokeWidth={2}
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </Svg>
      );
    case 'robot':
      return (
        <Svg {...comun}>
          <Circle cx={12} cy={12} r={8} fill="none" stroke={color} strokeWidth={2} />
          <Circle cx={12} cy={12} r={3} fill={color} />
        </Svg>
      );
    case 'limpiar':
      return (
        <Svg {...comun}>
          <Line x1={5} y1={5} x2={19} y2={19} stroke={color} strokeWidth={2} strokeLinecap="round" />
          <Line x1={19} y1={5} x2={5} y2={19} stroke={color} strokeWidth={2} strokeLinecap="round" />
        </Svg>
      );
  }
}
