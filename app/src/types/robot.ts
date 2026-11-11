export type Pose = {
  x: number;
  y: number;
  theta: number;
};

export type Sensors = {
  front: number;
  left: number;
  right: number;
  gas: number;
  gasAlerta: boolean;
  gasPpm: number;
};

export type RobotData = {
  pose: Pose;
  sensors: Sensors;
  timestamp: number;
};

export type LecturaDB = {
  id: number;
  timestamp: string;
  x_cm: number;
  y_cm: number;
  theta_rad: number;
  gas_valor: number;
  gas_alerta: boolean;
  gas_ppm: number | null;
  front_cm: number;
  left_cm: number;
  right_cm: number;
  robot_id: string;
};

export type Punto = { x: number; y: number };
