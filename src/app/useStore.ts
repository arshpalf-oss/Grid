import { useSyncExternalStore } from 'react';
import { store, UIState } from './store';

export function useStore(): UIState {
  return useSyncExternalStore(store.subscribe, store.getSnapshot, store.getSnapshot);
}
