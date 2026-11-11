// Reanimated 4 usa el plugin de react-native-worklets;
// Reanimated 3 usa react-native-reanimated/plugin.
let reanimatedPlugin = 'react-native-reanimated/plugin';
try {
  require.resolve('react-native-worklets/plugin');
  reanimatedPlugin = 'react-native-worklets/plugin';
} catch {
  // Reanimated 3
}

module.exports = {
  presets: ['module:@react-native/babel-preset'],
  plugins: [reanimatedPlugin],
};
